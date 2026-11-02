import Cocoa
import Vision
import CoreImage

func removeBackground(inputPath: String, outputPath: String) -> Bool {
    let fileUrl = URL(fileURLWithPath: inputPath)
    guard let ciImage = CIImage(contentsOf: fileUrl) else {
        fputs("Failed to load image: \(inputPath)\n", stderr)
        return false
    }

    if #available(macOS 14.0, *) {
        let request = VNGenerateForegroundInstanceMaskRequest()
        let handler = VNImageRequestHandler(ciImage: ciImage, options: [:])
        do {
            try handler.perform([request])
            if let observation = request.results?.first {
                let maskBuffer = try observation.generateScaledMaskForImage(forInstances: observation.allInstances, from: handler)
                let maskImage = CIImage(cvPixelBuffer: maskBuffer)
                
                let filter = CIFilter(name: "CIBlendWithMask")!
                filter.setValue(ciImage, forKey: kCIInputImageKey)
                filter.setValue(CIImage.empty(), forKey: kCIInputBackgroundImageKey)
                filter.setValue(maskImage, forKey: kCIInputMaskImageKey)
                
                guard let outputImage = filter.outputImage else { return false }
                let context = CIContext(options: nil)
                let colorSpace = CGColorSpaceCreateDeviceRGB()
                let outUrl = URL(fileURLWithPath: outputPath)
                try context.writePNGRepresentation(of: outputImage, to: outUrl, format: .RGBA8, colorSpace: colorSpace, options: [:])
                return true
            }
        } catch {
            fputs("Vision error: \(error.localizedDescription)\n", stderr)
        }
    }
    
    // 如果没有检测到前景主体，尝试使用人像分割请求 VNGeneratePersonSegmentationRequest
    if #available(macOS 12.0, *) {
        let personRequest = VNGeneratePersonSegmentationRequest()
        personRequest.qualityLevel = .accurate
        let handler = VNImageRequestHandler(ciImage: ciImage, options: [:])
        do {
            try handler.perform([personRequest])
            if let observation = personRequest.results?.first {
                let maskBuffer = observation.pixelBuffer
                let maskImage = CIImage(cvPixelBuffer: maskBuffer)
                
                // 缩放 mask 到图像原始大小
                let scaleX = ciImage.extent.width / maskImage.extent.width
                let scaleY = ciImage.extent.height / maskImage.extent.height
                let scaledMask = maskImage.transformed(by: CGAffineTransform(scaleX: scaleX, y: scaleY))
                
                let filter = CIFilter(name: "CIBlendWithMask")!
                filter.setValue(ciImage, forKey: kCIInputImageKey)
                filter.setValue(CIImage.empty(), forKey: kCIInputBackgroundImageKey)
                filter.setValue(scaledMask, forKey: kCIInputMaskImageKey)
                
                guard let outputImage = filter.outputImage else { return false }
                let context = CIContext(options: nil)
                let colorSpace = CGColorSpaceCreateDeviceRGB()
                let outUrl = URL(fileURLWithPath: outputPath)
                try context.writePNGRepresentation(of: outputImage, to: outUrl, format: .RGBA8, colorSpace: colorSpace, options: [:])
                return true
            }
        } catch {
            fputs("Person segmentation error: \(error.localizedDescription)\n", stderr)
        }
    }
    
    return false
}

let args = CommandLine.arguments
if args.count < 3 {
    print("Usage: mac_cutout <inputPath> <outputPath>")
    exit(1)
}

let success = removeBackground(inputPath: args[1], outputPath: args[2])
exit(success ? 0 : 2)
