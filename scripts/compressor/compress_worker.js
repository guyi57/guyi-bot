const fs = require('fs');
const path = require('path');
const os = require('os');
const sharp = require('sharp');

// 支持的图片扩展名
const SUPPORTED_EXTS = new Set(['.png', '.jpg', '.jpeg', '.webp', '.avif', '.gif', '.tiff']);

function isImageFile(filePath) {
  const ext = path.extname(filePath).toLowerCase();
  return SUPPORTED_EXTS.has(ext);
}

function scanImages(dirPath, recursive = true) {
  let results = [];
  try {
    const entries = fs.readdirSync(dirPath, { withFileTypes: true });
    for (const entry of entries) {
      const fullPath = path.join(dirPath, entry.name);
      if (entry.isDirectory()) {
        if (recursive && !entry.name.startsWith('.')) {
          results = results.concat(scanImages(fullPath, recursive));
        }
      } else if (entry.isFile() && isImageFile(fullPath)) {
        // 排除自身生成的 .min 文件
        if (!entry.name.includes('.min.')) {
          results.push(fullPath);
        }
      }
    }
  } catch (err) {
    console.error('scanImages error:', err.message);
  }
  return results;
}

function formatBytes(bytes) {
  if (bytes <= 0) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

async function processSingleFile(fileItem, config, baseDir = null) {
  const filePath = typeof fileItem === 'object' ? fileItem.path : fileItem;
  const originalRefPath = (typeof fileItem === 'object' && fileItem.originalPath) ? fileItem.originalPath : filePath;
  const stat = fs.statSync(filePath);
  const originalStat = fs.existsSync(originalRefPath) ? fs.statSync(originalRefPath) : stat;
  const originalSize = originalStat.size;
  const ext = path.extname(originalRefPath).toLowerCase();
  const dir = path.dirname(originalRefPath);
  const baseName = path.basename(originalRefPath, ext);

  let targetExt = ext;
  if (config.format && config.format !== 'original') {
    targetExt = '.' + config.format.toLowerCase();
    if (targetExt === '.jpg') targetExt = '.jpeg';
  }

  // 确定输出路径
  let outputPath;
  const saveMode = Number(config.saveMode) || 2; // 1: 替换, 2: 副本, 3: 指定目录

  if (saveMode === 1) {
    // 替换原文件
    if (targetExt === ext) {
      outputPath = path.join(dir, `${baseName}.tmp_${Date.now()}${targetExt}`);
    } else {
      outputPath = path.join(dir, `${baseName}${targetExt}`);
    }
  } else if (saveMode === 2) {
    // 副本模式
    outputPath = path.join(dir, `${baseName}.min${targetExt}`);
  } else if (saveMode === 3) {
    // 指定输出文件夹
    const outDir = config.customOutputDir || path.join(dir, 'compressed_output');
    if (baseDir && originalRefPath.startsWith(baseDir)) {
      const relSubDir = path.relative(baseDir, dir);
      const targetSubDir = path.join(outDir, relSubDir);
      fs.mkdirSync(targetSubDir, { recursive: true });
      outputPath = path.join(targetSubDir, `${baseName}${targetExt}`);
    } else {
      fs.mkdirSync(outDir, { recursive: true });
      outputPath = path.join(outDir, `${baseName}${targetExt}`);
    }
  }

  // 构建 sharp 转换管道
  let pipeline = sharp(filePath);

  // 尺寸调整
  if (config.resizeWidth > 0 || config.resizeHeight > 0) {
    pipeline = pipeline.resize({
      width: config.resizeWidth > 0 ? Number(config.resizeWidth) : undefined,
      height: config.resizeHeight > 0 ? Number(config.resizeHeight) : undefined,
      fit: 'inside',
      withoutEnlargement: true
    });
  }

  const quality = config.quality ? Math.min(100, Math.max(1, Number(config.quality))) : 82;
  const lossless = !!config.lossless;
  const effort = config.effort !== undefined ? Number(config.effort) : 4;

  const targetFormatClean = targetExt.replace('.', '');
  if (targetFormatClean === 'webp') {
    pipeline = pipeline.webp({
      quality: quality,
      lossless: lossless,
      effort: Math.min(6, Math.max(0, effort))
    });
  } else if (targetFormatClean === 'avif') {
    pipeline = pipeline.avif({
      quality: quality,
      lossless: lossless,
      effort: Math.min(9, Math.max(0, effort))
    });
  } else if (targetFormatClean === 'png') {
    if (lossless) {
      pipeline = pipeline.png({
        compressionLevel: 9,
        adaptiveFiltering: true,
        effort: 8
      });
    } else {
      // 视觉压缩：启用基于 imagequant 的高品质调色板量化（同 pngquant 原理），大幅缩减体积
      pipeline = pipeline.png({
        palette: true,
        quality: quality,
        compressionLevel: 9,
        effort: 7,
        dither: 1.0
      });
    }
  } else if (targetFormatClean === 'jpeg' || targetFormatClean === 'jpg') {
    pipeline = pipeline.jpeg({
      quality: quality,
      mozjpeg: true
    });
  }

  // 执行写入
  await pipeline.toFile(outputPath);

  // 检查生成文件大小
  let newStat = fs.statSync(outputPath);
  let compressedSize = newStat.size;

  // 智能防负向膨胀保护：如果输出格式与原图相同，且体积反而变大了，说明原图已经是极限压缩，保留原图
  if (compressedSize >= originalSize && targetExt === ext) {
    if (saveMode === 1) {
      if (outputPath !== filePath && fs.existsSync(outputPath)) {
        try { fs.unlinkSync(outputPath); } catch (e) {}
      }
      outputPath = filePath;
      compressedSize = originalSize;
    } else {
      try {
        fs.copyFileSync(filePath, outputPath);
        compressedSize = originalSize;
      } catch (e) {}
    }
  } else {
    // 正常瘦身成功
    if (saveMode === 1) {
      if (targetExt === ext) {
        fs.renameSync(outputPath, originalRefPath);
        outputPath = originalRefPath;
      } else {
        try {
          if (fs.existsSync(originalRefPath)) {
            fs.unlinkSync(originalRefPath);
          }
        } catch (e) {
          console.error('Failed to unlink original:', e.message);
        }
      }
    }
  }

  const savedBytes = Math.max(0, originalSize - compressedSize);
  const savedPercent = originalSize > 0 ? ((savedBytes / originalSize) * 100).toFixed(1) : 0;

  return {
    originalPath: originalRefPath,
    outputPath: outputPath,
    originalSize: originalSize,
    compressedSize: compressedSize,
    savedBytes: savedBytes,
    savedPercent: parseFloat(savedPercent)
  };
}

async function main() {
  let inputData = '';
  if (process.argv[2]) {
    inputData = process.argv[2];
  } else {
    // 从 stdin 读取
    inputData = fs.readFileSync(0, 'utf-8');
  }

  let config;
  try {
    config = JSON.parse(inputData);
  } catch (err) {
    console.error(JSON.stringify({ success: false, error: 'Invalid JSON config: ' + err.message }));
    process.exit(1);
  }

  // 独立实时单图预览模式 (以 5ms 速度返回真实压缩数据与 base64 图像)
  if (config.mode === 'preview') {
    const filePath = config.filePath;
    if (!filePath || !fs.existsSync(filePath)) {
      console.log(JSON.stringify({ success: false, error: 'File not found for preview' }));
      return;
    }
    const stat = fs.statSync(filePath);
    const originalSize = stat.size;
    const ext = path.extname(filePath).toLowerCase();

    let targetExt = ext;
    if (config.format && config.format !== 'original') {
      targetExt = '.' + config.format.toLowerCase();
      if (targetExt === '.jpg') targetExt = '.jpeg';
    }

    let pipeline = sharp(filePath);
    const meta = await pipeline.metadata();

    if (config.resizeWidth > 0 || config.resizeHeight > 0) {
      pipeline = pipeline.resize({
        width: config.resizeWidth > 0 ? Number(config.resizeWidth) : undefined,
        height: config.resizeHeight > 0 ? Number(config.resizeHeight) : undefined,
        fit: 'inside',
        withoutEnlargement: true
      });
    }

    const quality = config.quality ? Math.min(100, Math.max(1, Number(config.quality))) : 82;
    const lossless = !!config.lossless;
    const effort = config.effort !== undefined ? Number(config.effort) : 4;

    const targetFormatClean = targetExt.replace('.', '');
    if (targetFormatClean === 'webp') {
      pipeline = pipeline.webp({ quality, lossless, effort: Math.min(6, Math.max(0, effort)) });
    } else if (targetFormatClean === 'avif') {
      pipeline = pipeline.avif({ quality, lossless, effort: Math.min(9, Math.max(0, effort)) });
    } else if (targetFormatClean === 'png') {
      if (lossless) {
        pipeline = pipeline.png({ compressionLevel: 9, adaptiveFiltering: true, effort: 8 });
      } else {
        pipeline = pipeline.png({ palette: true, quality, compressionLevel: 9, effort: 7 });
      }
    } else if (targetFormatClean === 'jpeg' || targetFormatClean === 'jpg') {
      pipeline = pipeline.jpeg({ quality, mozjpeg: true });
    }

    const buf = await pipeline.toBuffer();
    let compSize = buf.length;
    if (compSize >= originalSize && targetExt === ext) {
      compSize = originalSize;
    }
    const savedBytes = Math.max(0, originalSize - compSize);
    const savedPercent = originalSize > 0 ? ((savedBytes / originalSize) * 100).toFixed(1) : 0;
    const mime = targetFormatClean === 'jpg' ? 'jpeg' : targetFormatClean;

    console.log(JSON.stringify({
      success: true,
      originalSize,
      compressedSize: compSize,
      savedBytes,
      savedPercent: parseFloat(savedPercent),
      originalWidth: meta.width,
      originalHeight: meta.height,
      dataUrl: `data:image/${mime};base64,` + buf.toString('base64')
    }));
    return;
  }

  // 智能抠图模式 (优先使用 macOS Apple Vision 神经引擎，备选高精度色彩去底)
  if (config.mode === 'cutout') {
    const filePath = config.filePath;
    if (!filePath || !fs.existsSync(filePath)) {
      console.log(JSON.stringify({ success: false, error: 'File not found for cutout' }));
      return;
    }

    const tmpOut = path.join(os.tmpdir(), `cutout_${Date.now()}_${Math.random().toString(36).substring(7)}.png`);
    const macCutoutBin = path.join(__dirname, 'mac_cutout');

    let aiSuccess = false;
    if (fs.existsSync(macCutoutBin) && config.type !== 'color') {
      try {
        const { execFileSync } = require('child_process');
        execFileSync(macCutoutBin, [filePath, tmpOut], { stdio: 'pipe', timeout: 15000 });
        if (fs.existsSync(tmpOut) && fs.statSync(tmpOut).size > 0) {
          aiSuccess = true;
        }
      } catch (err) {
        // AI mask didn't detect salient object
      }
    }

    if (aiSuccess) {
      const buf = fs.readFileSync(tmpOut);
      console.log(JSON.stringify({
        success: true,
        method: 'apple_vision_ai',
        tempPath: tmpOut,
        dataUrl: 'data:image/png;base64,' + buf.toString('base64'),
        size: buf.length
      }));
      return;
    }

    // 智能背景去底 (智能识别背景颜色并透明化)
    try {
      const { data, info } = await sharp(filePath).ensureAlpha().raw().toBuffer({ resolveWithObject: true });
      const channels = info.channels; // 4
      const width = info.width;
      const height = info.height;

      // 读取左上角像素作为基准背景色
      const r0 = data[0], g0 = data[1], b0 = data[2];
      const tolerance = Number(config.tolerance) || 35;
      const tolSq = tolerance * tolerance * 3;

      for (let i = 0; i < data.length; i += channels) {
        const r = data[i], g = data[i+1], b = data[i+2];
        const distSq = (r - r0)**2 + (g - g0)**2 + (b - b0)**2;
        if (distSq < tolSq) {
          // 柔化边缘
          const factor = Math.sqrt(distSq / tolSq);
          data[i+3] = Math.round(255 * factor);
          if (distSq < (tolerance * 0.7)**2 * 3) {
            data[i+3] = 0;
          }
        }
      }

      const outBuf = await sharp(data, {
        raw: { width, height, channels }
      }).png({ compressionLevel: 9 }).toBuffer();

      try {
        fs.writeFileSync(tmpOut, outBuf);
      } catch (e) {}

      console.log(JSON.stringify({
        success: true,
        method: 'smart_color_matting',
        tempPath: tmpOut,
        dataUrl: 'data:image/png;base64,' + outBuf.toString('base64'),
        size: outBuf.length
      }));
      return;
    } catch (err) {
      console.log(JSON.stringify({
        success: false,
        error: '抠图处理失败: ' + err.message
      }));
      return;
    }
  }

  let fileList = [];
  let baseDir = null;

  if (config.files && Array.isArray(config.files) && config.files.length > 0) {
    fileList = config.files.filter(f => {
      const p = typeof f === 'object' ? f.path : f;
      return p && fs.existsSync(p) && isImageFile(p);
    });
  } else if (config.targetPath && fs.existsSync(config.targetPath)) {
    const targetStat = fs.statSync(config.targetPath);
    if (targetStat.isDirectory()) {
      baseDir = config.targetPath;
      fileList = scanImages(config.targetPath, true);
    } else if (targetStat.isFile() && isImageFile(config.targetPath)) {
      fileList = [config.targetPath];
    }
  }

  if (fileList.length === 0) {
    console.log(JSON.stringify({
      success: false,
      error: '没有找到任何可处理的图片文件'
    }));
    return;
  }

  const results = [];
  const errors = [];
  let totalOrigBytes = 0;
  let totalCompBytes = 0;

  // 并发控制（限制为 CPU 核心数）
  const concurrency = Math.max(2, os.cpus().length);
  for (let i = 0; i < fileList.length; i += concurrency) {
    const chunk = fileList.slice(i, i + concurrency);
    const promises = chunk.map(async (file) => {
      try {
        const res = await processSingleFile(file, config, baseDir);
        results.push(res);
        totalOrigBytes += res.originalSize;
        totalCompBytes += res.compressedSize;
      } catch (err) {
        errors.push({ file, error: err.message });
      }
    });
    await Promise.all(promises);
  }

  const totalSavedBytes = totalOrigBytes - totalCompBytes;
  const savedPercent = totalOrigBytes > 0 ? ((totalSavedBytes / totalOrigBytes) * 100).toFixed(1) : 0;

  const outputSummary = {
    success: true,
    totalFiles: fileList.length,
    processedCount: results.length,
    failedCount: errors.length,
    originalTotalBytes: totalOrigBytes,
    originalTotalFormatted: formatBytes(totalOrigBytes),
    compressedTotalBytes: totalCompBytes,
    compressedTotalFormatted: formatBytes(totalCompBytes),
    savedBytes: totalSavedBytes,
    savedBytesFormatted: formatBytes(Math.max(0, totalSavedBytes)),
    savedPercent: parseFloat(savedPercent),
    saveMode: config.saveMode || 2,
    details: results,
    errors: errors
  };

  console.log(JSON.stringify(outputSummary));
}

main().catch(err => {
  console.error(JSON.stringify({ success: false, error: err.message }));
  process.exit(1);
});
