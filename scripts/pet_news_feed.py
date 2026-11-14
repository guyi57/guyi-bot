#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
桌宠资讯与热梗聚合器（零外部依赖，使用 Python 标准库）
实测免费可用源：
  - 60s API 简报: https://60s.viki.moe/v2/60s (每日 15 条新闻简报与微语)
  - 微博热搜榜:   https://60s.viki.moe/v2/weibo
  - 知乎热榜:     https://60s.viki.moe/v2/zhihu
  - 今日头条:     https://60s.viki.moe/v2/toutiao
  - 抖音热点:     https://60s.viki.moe/v2/douyin
  - V2EX 程序员热门: https://www.v2ex.com/api/topics/hot.json
  - 一言/微语爆梗: https://60s.viki.moe/v2/hitokoto
  - Hacker News:  https://hacker-news.firebaseio.com/v0/topstories.json

用法：
  python3 pet_news_feed.py            # 随机输出一条纯文本（混排）
  python3 pet_news_feed.py json       # 随机输出一条 JSON 格式 {"source": ..., "text": ..., "url": ...}
  python3 pet_news_feed.py news       # 随机输出一条今日新闻
  python3 pet_news_feed.py hot        # 随机输出一条热榜
  python3 pet_news_feed.py fun        # 随机输出一条趣味/技术热帖
  python3 pet_news_feed.py all        # 刷新并输出所有源的概览
"""

import json
import os
import random
import sys
import time
import urllib.request
import urllib.error

CACHE_FILE = os.path.expanduser("~/.pet_news_cache.json")
TTL = 600  # 10 分钟缓存有效期，避免频繁打扰免费接口

UA = "DesktopPet/1.0 (guyi-bot; macOS; +https://example.local)"


def _get_json(url, timeout=8):
    """带 Cloudflare 1015 限流退避重试"""
    last_err = None
    for attempt in range(3):
        try:
            req = urllib.request.Request(
                url,
                headers={"User-Agent": UA, "Accept": "application/json"}
            )
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                return json.loads(resp.read().decode("utf-8", "replace"))
        except Exception as e:
            last_err = e
            time.sleep(1.2 * (attempt + 1))
    raise last_err


def _cached(key, url, picker, ttl=TTL):
    """带本地磁盘缓存的抓取，若网络失败则自动回退至历史缓存"""
    cache = {}
    if os.path.exists(CACHE_FILE):
        try:
            with open(CACHE_FILE, "r", encoding="utf-8") as f:
                cache = json.load(f)
        except Exception:
            cache = {}

    hit = cache.get(key)
    if hit and time.time() - hit.get("ts", 0) < ttl:
        return hit.get("items", [])

    try:
        data = _get_json(url)
        items = picker(data)
        if items:
            cache[key] = {"ts": time.time(), "items": items}
            try:
                with open(CACHE_FILE, "w", encoding="utf-8") as f:
                    json.dump(cache, f, ensure_ascii=False)
            except Exception:
                pass
            return items
    except Exception:
        pass

    return hit.get("items", []) if hit else []


def pick_60s(d):
    news = (d.get("data") or {}).get("news", [])
    return [{"source": "60秒新闻", "text": x, "url": "https://60s.viki.moe"} for x in news if isinstance(x, str)]


def make_picker(source_name, limit=15):
    def _picker(d):
        out = []
        for it in (d.get("data") or [])[:limit]:
            if isinstance(it, dict) and it.get("title"):
                out.append({
                    "source": source_name,
                    "text": it["title"].strip(),
                    "url": it.get("link") or it.get("url") or ""
                })
        return out
    return _picker


def pick_v2ex(d):
    out = []
    for it in (d or [])[:15]:
        if isinstance(it, dict) and it.get("title"):
            out.append({
                "source": "V2EX热门",
                "text": it["title"].strip(),
                "url": it.get("url", "")
            })
    return out


def pick_hitokoto(d):
    s = (d.get("data") or {}).get("hitokoto")
    if s:
        return [{"source": "每日微语", "text": s.strip(), "url": ""}]
    return []


def fetch_hn():
    cache = {}
    if os.path.exists(CACHE_FILE):
        try:
            with open(CACHE_FILE, "r", encoding="utf-8") as f:
                cache = json.load(f)
        except Exception:
            cache = {}
    hit = cache.get("hn_items")
    if hit and time.time() - hit.get("ts", 0) < TTL:
        return hit.get("items", [])

    try:
        req = urllib.request.Request("https://hacker-news.firebaseio.com/v0/topstories.json", headers={"User-Agent": UA})
        with urllib.request.urlopen(req, timeout=3) as resp:
            ids = json.loads(resp.read().decode("utf-8"))[:6]
        out = []
        for i in ids[:4]:
            try:
                r_item = urllib.request.Request(f"https://hacker-news.firebaseio.com/v0/item/{i}.json", headers={"User-Agent": UA})
                with urllib.request.urlopen(r_item, timeout=2.5) as r2:
                    item = json.loads(r2.read().decode("utf-8"))
                    if item and item.get("title"):
                        out.append({
                            "source": "HackerNews",
                            "text": item["title"].strip(),
                            "url": item.get("url") or f"https://news.ycombinator.com/item?id={i}"
                        })
            except Exception:
                continue
        if out:
            cache["hn_items"] = {"ts": time.time(), "items": out}
            with open(CACHE_FILE, "w", encoding="utf-8") as f:
                json.dump(cache, f, ensure_ascii=False)
            return out
    except Exception:
        pass
    return hit.get("items", []) if hit else []


SOURCE_NAMES = {
    "60s": "60秒新闻",
    "news": "60秒新闻",
    "weibo": "微博热搜",
    "zhihu": "知乎热榜",
    "toutiao": "今日头条",
    "douyin": "抖音热点",
    "v2ex": "V2EX热门",
    "hitokoto": "每日微语",
    "hacker-news": "HackerNews",
    "hn_items": "HackerNews"
}

SOURCES = {
    "news": ("60s", "https://60s.viki.moe/v2/60s", pick_60s),
    "weibo": ("weibo", "https://60s.viki.moe/v2/weibo", make_picker("微博热搜")),
    "zhihu": ("zhihu", "https://60s.viki.moe/v2/zhihu", make_picker("知乎热榜")),
    "toutiao": ("toutiao", "https://60s.viki.moe/v2/toutiao", make_picker("今日头条")),
    "douyin": ("douyin", "https://60s.viki.moe/v2/douyin", make_picker("抖音热点")),
    "v2ex": ("v2ex", "https://www.v2ex.com/api/topics/hot.json", pick_v2ex),
    "hitokoto": ("hitokoto", "https://60s.viki.moe/v2/hitokoto", pick_hitokoto),
}


def gather(kind):
    if kind == "news":
        keys = ["news"]
    elif kind == "hot":
        keys = ["weibo", "zhihu", "toutiao", "douyin"]
    elif kind == "fun":
        keys = ["v2ex", "hitokoto"]
    else:
        keys = list(SOURCES.keys())

    pool = []
    for k in keys:
        name, url, picker = SOURCES[k]
        items = _cached(name, url, picker)
        for it in items:
            if not it.get("source"):
                it["source"] = SOURCE_NAMES.get(name, SOURCE_NAMES.get(k, "全网热点"))
            it["category"] = kind
        pool += items

    if kind in ("fun", "mix"):
        hn_items = fetch_hn()
        for it in hn_items:
            it.setdefault("source", "HackerNews")
            it["category"] = "fun"
        pool += hn_items

    return pool


def main():
    kind = sys.argv[1] if len(sys.argv) > 1 else "mix"

    if kind == "all":
        for k in ["news", "weibo", "zhihu", "toutiao", "douyin", "v2ex", "hitokoto"]:
            name, url, picker = SOURCES[k]
            items = _cached(name, url, picker, ttl=60)
            print(f"\n[{name}] {len(items)} 条")
            for it in items[:3]:
                print(f"  - [{it.get('source', k)}] {it['text'][:60]}")
        hn = fetch_hn()
        print(f"\n[hackernews] {len(hn)} 条")
        for it in hn[:3]:
            print(f"  - [{it.get('source', 'HN')}] {it['text'][:60]}")
        return

    if kind == "json":
        pools = [gather("news"), gather("hot"), gather("fun")]
        pools = [p for p in pools if p]
        if not pools:
            print(json.dumps({"source": "桌宠闲聊", "text": "今天网线好像被猫啃了，先休息会吧~", "url": ""}, ensure_ascii=False))
            return
        chosen = random.choice(random.choice(pools))
        print(json.dumps(chosen, ensure_ascii=False))
        return

    if kind == "mix":
        pools = [gather("news"), gather("hot"), gather("fun")]
        pools = [p for p in pools if p]
        pool = random.choice(pools) if pools else []
    else:
        pool = gather(kind)

    if not pool:
        print("（桌宠：今天网线好像被猫啃了，先休息会吧…）")
        return

    chosen = random.choice(pool)
    print(f"[{chosen.get('source', '热点')}] {chosen['text']}")


if __name__ == "__main__":
    main()
