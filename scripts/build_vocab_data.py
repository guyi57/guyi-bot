# -*- coding: utf-8 -*-
"""
Generate curated vocabulary datasets for:
1. 专升本英语高频核心必背词 (vocab_upgrade_exam.json)
2. 程序员开发高频实战词汇 (vocab_programming.json)
"""

import json
import os

# 1. 专升本/四级高频核心考点词 (精选核心重点词，含音标、词性、核心中文、真题例句、记忆法)
UPGRADE_EXAM_WORDS = [
    {
        "word": "feasible",
        "phonetic": "/ˈfiːzəbl/",
        "pos": "adj.",
        "definition": "可行的；行得通的；做得到的",
        "example": "It is quite feasible to implement this algorithm in our project.",
        "memory_tip": "fee(费用) + able(能) → 能承担起费用的方案才是'可行的'。"
    },
    {
        "word": "preliminary",
        "phonetic": "/prɪˈlɪmɪneri/",
        "pos": "adj.",
        "definition": "初步的；预备的；预考",
        "example": "The preliminary results of the experiment look very promising.",
        "memory_tip": "pre(在前) + limin(门槛/界限) + ary → 进门槛之前的 → '初步的'。"
    },
    {
        "word": "sophisticated",
        "phonetic": "/səˈfɪstɪkeɪtɪd/",
        "pos": "adj.",
        "definition": "复杂的；精密的；老练的",
        "example": "Modern operating systems use sophisticated scheduling algorithms.",
        "memory_tip": "sophi(智慧) + sticate → 充满智慧精巧的 → '复杂的，精密的'。"
    },
    {
        "word": "inevitable",
        "phonetic": "/ɪnˈevɪtəbl/",
        "pos": "adj.",
        "definition": "不可避免的；必然发生的",
        "example": "Conflict between processes is inevitable in concurrent programming.",
        "memory_tip": "in(不) + evit(避开) + able(可...的) → 无法避开的 → '不可避免的'。"
    },
    {
        "word": "subsequent",
        "phonetic": "/ˈsʌbsɪkwənt/",
        "pos": "adj.",
        "definition": "随后的；后来的；接着的",
        "example": "Subsequent tests confirmed the existence of the memory leak.",
        "memory_tip": "sub(在后) + sequ(跟随) + ent → 跟在后面的 → '随后的'。"
    },
    {
        "word": "undermine",
        "phonetic": "/ˌʌndərˈmaɪn/",
        "pos": "v.",
        "definition": "逐渐削弱；暗中破坏；从根基损害",
        "example": "Frequent crashes will severely undermine user trust.",
        "memory_tip": "under(在下面) + mine(挖地雷/矿) → 在底下暗中挖空 → '暗中破坏，削弱'。"
    },
    {
        "word": "accommodate",
        "phonetic": "/əˈkɑːmədeɪt/",
        "pos": "v.",
        "definition": "容纳；适应；顺应；提供住宿",
        "example": "The new server architecture can easily accommodate millions of users.",
        "memory_tip": "ac(朝向) + commod(便利/舒适) + ate → 使对方方便舒适 → '容纳，适应'。"
    },
    {
        "word": "compensate",
        "phonetic": "/ˈkɑːmpenseɪt/",
        "pos": "v.",
        "definition": "补偿；赔偿；弥补",
        "example": "His diligence compensates for his lack of programming experience.",
        "memory_tip": "com(共同) + pens(称重/衡量) + ate → 把天平两边称平 → '弥补，补偿'。"
    },
    {
        "word": "eligible",
        "phonetic": "/ˈelɪdʒəbl/",
        "pos": "adj.",
        "definition": "有资格的；合格的；符合条件的",
        "example": "Only registered students are eligible for the scholarship.",
        "memory_tip": "e(出) + lig(挑选) + ible → 可以挑选出来的优秀人选 → '有资格的'。"
    },
    {
        "word": "vague",
        "phonetic": "/veɪɡ/",
        "pos": "adj.",
        "definition": "模糊的；不明确的；含糊的",
        "example": "The error message is too vague to pinpoint the bug.",
        "memory_tip": "谐音'未果' → 事情没结果，说话总是'含糊模糊的'。"
    },
    {
        "word": "advocate",
        "phonetic": "/ˈædvəkeɪt/",
        "pos": "v./n.",
        "definition": "拥护；主张；提倡；提倡者",
        "example": "He advocates using unit tests to ensure software quality.",
        "memory_tip": "ad(朝向) + voc(声音) + ate → 朝大众发出声音呼吁 → '主张，拥护'。"
    },
    {
        "word": "comply",
        "phonetic": "/kəmˈplaɪ/",
        "pos": "v.",
        "definition": "顺从；服从；遵守 (常与 with 连用)",
        "example": "The application must comply with privacy security regulations.",
        "memory_tip": "com(全部) + ply(弯曲) → 全身弯曲顺应别人 → '服从，遵守'。"
    },
    {
        "word": "deteriorate",
        "phonetic": "/dɪˈtɪriəreɪt/",
        "pos": "v.",
        "definition": "恶化；变坏；衰退",
        "example": "System performance will deteriorate rapidly if swap is exhausted.",
        "memory_tip": "de(向下) + terior(较劣) + ate → 往更劣质的方向发展 → '恶化'。"
    },
    {
        "word": "diminish",
        "phonetic": "/dɪˈmɪnɪʃ/",
        "pos": "v.",
        "definition": "减少；缩小；削弱",
        "example": "The delay diminished significantly after caching was introduced.",
        "memory_tip": "di(向下) + mini(小) + ish → 使之变小 → '减少，缩小'。"
    },
    {
        "word": "drastic",
        "phonetic": "/ˈdræstɪk/",
        "pos": "adj.",
        "definition": "严厉的；剧烈的；极端的",
        "example": "We need to take drastic measures to reduce memory footprint.",
        "memory_tip": "谐音'抓死' → 抓得特别死，手段非常'剧烈，严厉'。"
    },
    {
        "word": "elaborate",
        "phonetic": "/ɪˈlæbərət/",
        "pos": "adj./v.",
        "definition": "精巧复杂的；详尽阐述；精心制作",
        "example": "He gave an elaborate explanation of the consensus protocol.",
        "memory_tip": "e(出) + labor(劳动) + ate → 倾注了大量劳动的成果 → '精心制作的，详尽的'。"
    },
    {
        "word": "fluctuate",
        "phonetic": "/ˈflʌktʃueɪt/",
        "pos": "v.",
        "definition": "波动；涨落；起伏不定",
        "example": "Network latency tends to fluctuate during peak hours.",
        "memory_tip": "fluct(流动/波浪) + uate → 像波浪一样流动起伏 → '波动，起伏'。"
    },
    {
        "word": "hinder",
        "phonetic": "/ˈhɪndər/",
        "pos": "v.",
        "definition": "阻碍；妨碍；阻挡",
        "example": "Network congestion will hinder data synchronization.",
        "memory_tip": "hind(后面) + er → 总是从背后拉住别人 → '阻碍，妨碍'。"
    },
    {
        "word": "indispensable",
        "phonetic": "/ˌɪndɪˈspensəbl/",
        "pos": "adj.",
        "definition": "必不可少的；不可或缺的",
        "example": "Version control is indispensable for collaborative development.",
        "memory_tip": "in(不) + dispense(免除/分发) + able → 绝对不可免除的 → '必不可少的'。"
    },
    {
        "word": "legitimate",
        "phonetic": "/lɪˈdʒɪtɪmət/",
        "pos": "adj.",
        "definition": "合法的；正当的；合理的",
        "example": "The firewall only allows legitimate requests through.",
        "memory_tip": "leg(法律) + itim + ate → 符合法律规定的 → '合法的，正当的'。"
    },
    {
        "word": "negligible",
        "phonetic": "/ˈneɡlɪdʒəbl/",
        "pos": "adj.",
        "definition": "微不足道的；可以忽略的",
        "example": "The performance overhead of this wrapper is negligible.",
        "memory_tip": "neglect(忽视) + ible → 可以直接忽视掉的 → '微不足道的'。"
    },
    {
        "word": "oblige",
        "phonetic": "/əˈblaɪdʒ/",
        "pos": "v.",
        "definition": "强迫；使感激；施恩惠于",
        "example": "The contract obliges the company to provide free updates.",
        "memory_tip": "ob(加强) + lig(绑定) + e → 紧紧绑定你的责任 → '强迫，使负有义务'。"
    },
    {
        "word": "plausible",
        "phonetic": "/ˈplɔːzəbl/",
        "pos": "adj.",
        "definition": "貌似合理的；说得通的；令人信服的",
        "example": "His hypothesis provides a plausible explanation for the bug.",
        "memory_tip": "plaus(鼓掌) + ible → 听起来让人想鼓掌认可的 → '貌似合理的，可信的'。"
    },
    {
        "word": "refrain",
        "phonetic": "/rɪˈfreɪn/",
        "pos": "v.",
        "definition": "克制；抑制；忍住 (常与 from 连用)",
        "example": "Please refrain from committing untested code to the main branch.",
        "memory_tip": "re(回) + frain(拉紧缰绳) → 勒住奔腾的马头 → '克制，忍住'。"
    },
    {
        "word": "reluctant",
        "phonetic": "/rɪˈlʌktənt/",
        "pos": "adj.",
        "definition": "不情愿的；勉强的",
        "example": "Developers are often reluctant to write lengthy documentation.",
        "memory_tip": "re(反) + luct(抗争) + ant → 心里一直抗拒挣扎的 → '不情愿的'。"
    },
    {
        "word": "scrutinize",
        "phonetic": "/ˈskruːtənaɪz/",
        "pos": "v.",
        "definition": "仔细检查；彻底审查",
        "example": "The security team will scrutinize every pull request carefully.",
        "memory_tip": "scrut(细察) + inize → 拿着放大镜查到底 → '仔细审查'。"
    },
    {
        "word": "tangible",
        "phonetic": "/ˈtændʒəbl/",
        "pos": "adj.",
        "definition": "有形的；切实的；可触摸的",
        "example": "Refactoring delivered tangible improvements in execution speed.",
        "memory_tip": "tang(接触/触摸) + ible → 触手可及的 → '切实可见的，有形的'。"
    },
    {
        "word": "unanimous",
        "phonetic": "/juˈnænɪməs/",
        "pos": "adj.",
        "definition": "意见一致的；全体一致的",
        "example": "The committee reached a unanimous decision to release the software.",
        "memory_tip": "uni(单一/一致) + anim(心智/精神) + ous → 一条心的 → '一致同意的'。"
    },
    {
        "word": "versatile",
        "phonetic": "/ˈvɜːrsətl/",
        "pos": "adj.",
        "definition": "多才多艺的；多功能的；通用的",
        "example": "Python is a versatile language used in web, data, and AI.",
        "memory_tip": "vers(转变/转动) + atile → 怎么转弯都能适应的 → '多功能的，通用的'。"
    },
    {
        "word": "vulnerable",
        "phonetic": "/ˈvʌlnərəbl/",
        "pos": "adj.",
        "definition": "易受伤害的；脆弱的；有漏洞的",
        "example": "Unpatched systems are highly vulnerable to ransomware attacks.",
        "memory_tip": "vulner(创伤) + able → 容易受到创伤的 → '脆弱的，易受攻击的'。"
    },
    {
        "word": "abrupt",
        "phonetic": "/əˈbrʌpt/",
        "pos": "adj.",
        "definition": "突然的；意外的；唐突的",
        "example": "An abrupt shutdown may cause database corruption.",
        "memory_tip": "ab(离去) + rupt(断裂) → 突然断裂开来 → '突然的，唐突的'。"
    },
    {
        "word": "coherent",
        "phonetic": "/koʊˈhɪrənt/",
        "pos": "adj.",
        "definition": "连贯的；前后一致的；条理清楚的",
        "example": "We need a coherent strategy to migrate our legacy systems.",
        "memory_tip": "co(共同) + her(粘连) + ent → 紧密粘合在一起的 → '连贯一致的'。"
    },
    {
        "word": "distort",
        "phonetic": "/dɪˈstɔːrt/",
        "pos": "v.",
        "definition": "扭曲；曲解；变形",
        "example": "Lossy compression may distort the original image quality.",
        "memory_tip": "dis(离开) + tort(扭转) → 扭到一边变形了 → '扭曲，曲解'。"
    },
    {
        "word": "exhaust",
        "phonetic": "/ɪɡˈzɔːst/",
        "pos": "v./n.",
        "definition": "耗尽；用光；使筋疲力尽；废气",
        "example": "Infinite recursion will exhaust stack memory quickly.",
        "memory_tip": "ex(出) + haust(吸/抽) → 全都吸抽出来了 → '耗尽，用光'。"
    },
    {
        "word": "fundamental",
        "phonetic": "/ˌfʌndəˈmentl/",
        "pos": "adj./n.",
        "definition": "根本的；基础的；核心原则",
        "example": "Data structures and algorithms are fundamental to programming.",
        "memory_tip": "fund(基础/基金) + ament + al → 最底层的根基 → '根本的，基础的'。"
    },
    {
        "word": "hypothetical",
        "phonetic": "/ˌhaɪpəˈθetɪkl/",
        "pos": "adj.",
        "definition": "假设的；假定的",
        "example": "Let's consider a hypothetical scenario where the database fails.",
        "memory_tip": "hypo(在下) + thet(放置) + ical → 放在底层作为前提的 → '假定的，假设的'。"
    },
    {
        "word": "intrinsic",
        "phonetic": "/ɪnˈtrɪnzɪk/",
        "pos": "adj.",
        "definition": "固有的；内在的；本质的",
        "example": "Curiosity is an intrinsic motivation for continuous learning.",
        "memory_tip": "intra(在内部) + in + sic → 生来就存在于内部的 → '固有的，本质的'。"
    },
    {
        "word": "manifest",
        "phonetic": "/ˈmænɪfest/",
        "pos": "v./adj.",
        "definition": "显现；证明；明显的；清单",
        "example": "The memory bug manifested only under heavy network load.",
        "memory_tip": "mani(手) + fest(击打/抓住) → 用手就能摸到感受到的 → '明显的，显现'。"
    },
    {
        "word": "obsolete",
        "phonetic": "/ˌɑːbsəˈliːt/",
        "pos": "adj.",
        "definition": "废弃的；淘汰的；过时的",
        "example": "The legacy protocol is now obsolete and replaced by HTTP/2.",
        "memory_tip": "ob(反) + sole(单一/习惯) + te → 离开日常习惯不再被使用的 → '过时的'。"
    },
    {
        "word": "perpetual",
        "phonetic": "/pərˈpetʃuəl/",
        "pos": "adj.",
        "definition": "长期的；永恒的；终身的",
        "example": "He entered a perpetual loop because the exit condition was missing.",
        "memory_tip": "per(始终) + pet(追求/前往) + ual → 始终不停向前的 → '永恒的，无休止的'。"
    },
    {
        "word": "rigid",
        "phonetic": "/ˈrɪdʒɪd/",
        "pos": "adj.",
        "definition": "僵硬的；刻板的；死板的",
        "example": "A software architecture that is too rigid cannot adapt to change.",
        "memory_tip": "rig(僵硬) + id → 无法弯折变化的 → '死板僵硬的'。"
    },
    {
        "word": "spontaneous",
        "phonetic": "/spɑːnˈteɪniəs/",
        "pos": "adj.",
        "definition": "自发的；自然的；本能的",
        "example": "The pet made a spontaneous playful jump to greet the user.",
        "memory_tip": "spon(承诺/自愿) + taneous → 自觉自愿行动的 → '自发的，自然的'。"
    },
    {
        "word": "trivial",
        "phonetic": "/ˈtrɪviəl/",
        "pos": "adj.",
        "definition": "琐碎的；不重要的；轻微的",
        "example": "Do not waste hours debating trivial naming conventions.",
        "memory_tip": "tri(三) + via(道路) + al → 三岔路口人们闲聊的碎嘴八卦 → '琐碎轻微的'。"
    },
    {
        "word": "underlie",
        "phonetic": "/ˌʌndərˈlaɪ/",
        "pos": "v.",
        "definition": "构成...的基础；作为...的原因",
        "example": "Mathematical rigor underlies the design of cryptography.",
        "memory_tip": "under(在下) + lie(平躺/位于) → 位于最底部作为支柱 → '构成...基础'。"
    },
    {
        "word": "withstand",
        "phonetic": "/wɪðˈstænd/",
        "pos": "v.",
        "definition": "经受住；承受；抵挡",
        "example": "The cloud service can withstand unexpected power outages.",
        "memory_tip": "with(对抗) + stand(站立) → 迎风立稳对抗阻力 → '经受住，承受'。"
    }
]

# 2. 程序员开发实战高频核心词汇 (精选编程高频术语、架构、报错、常用方法)
PROGRAMMING_WORDS = [
    {
        "word": "instantiate",
        "phonetic": "/ɪnˈstænʃieɪt/",
        "pos": "v.",
        "definition": "实例化；创建实例",
        "example": "You need to instantiate the class before accessing its member variables.",
        "memory_tip": "in(进入) + stance(实体/例子) + iate → 将抽象蓝图落入真实内存实体 → '实例化'。"
    },
    {
        "word": "deprecated",
        "phonetic": "/ˈdeprəkeɪtɪd/",
        "pos": "adj.",
        "definition": "已弃用的；不赞成使用的；已过时的",
        "example": "This method is deprecated in C++20 and will be removed in future versions.",
        "memory_tip": "de(向下) + prec(祈求) + ated → 祈祷开发者不要再用了 → '已弃用'。"
    },
    {
        "word": "concurrency",
        "phonetic": "/kənˈkɜːrənsi/",
        "pos": "n.",
        "definition": "并发；并发性；并发执行",
        "example": "Handling high concurrency requires robust mutex locking mechanisms.",
        "memory_tip": "con(共同) + curr(跑) + ency → 多个任务一起在跑 → '并发'。"
    },
    {
        "word": "asynchronous",
        "phonetic": "/eɪˈsɪŋkrənəs/",
        "pos": "adj.",
        "definition": "异步的；非同步的",
        "example": "Asynchronous network calls prevent the desktop GUI from freezing.",
        "memory_tip": "a(非) + syn(相同) + chron(时间) + ous → 不在同一个时间节奏等待 → '异步的'。"
    },
    {
        "word": "idempotent",
        "phonetic": "/ˌaɪdəmˈpoʊtnt/",
        "pos": "adj.",
        "definition": "幂等的 (多次调用效果相同)",
        "example": "The HTTP GET and DELETE methods are expected to be idempotent.",
        "memory_tip": "idem(相同) + potent(能力/能量) → 调用 1 次或 100 次产生的效果完全相同 → '幂等'。"
    },
    {
        "word": "recursion",
        "phonetic": "/rɪˈkɜːrʒn/",
        "pos": "n.",
        "definition": "递归；循环往复",
        "example": "Make sure your recursion has a clear base case to avoid stack overflow.",
        "memory_tip": "re(再次) + curs(跑) + ion → 函数自己再次跑入自己内部 → '递归'。"
    },
    {
        "word": "polymorphism",
        "phonetic": "/ˌpɑːliˈmɔːrfɪzəm/",
        "pos": "n.",
        "definition": "多态性；多形性",
        "example": "Virtual functions in C++ provide runtime polymorphism.",
        "memory_tip": "poly(多个) + morph(形态) + ism → 同一个接口展现多种形态行为 → '多态'。"
    },
    {
        "word": "immutable",
        "phonetic": "/ɪˈmjuːtəbl/",
        "pos": "adj.",
        "definition": "不可变的；不可修改的",
        "example": "Immutable data structures are inherently thread-safe.",
        "memory_tip": "im(不) + mute(突变/改变) + able → 无法更改其内容的 → '不可变的'。"
    },
    {
        "word": "latency",
        "phonetic": "/ˈleɪtnsi/",
        "pos": "n.",
        "definition": "延迟；潜伏时间；等待时间",
        "example": "The new cache reduced disk latency by more than 80 percent.",
        "memory_tip": "late(晚/迟) + ency → 迟到拖延的时间标量 → '延迟'。"
    },
    {
        "word": "throughput",
        "phonetic": "/ˈθruːpʊt/",
        "pos": "n.",
        "definition": "吞吐量；处理能力",
        "example": "Batch processing significantly improves system throughput.",
        "memory_tip": "through(穿过) + put(放置) → 单位时间内能穿流过去的数据总量 → '吞吐量'。"
    },
    {
        "word": "deadlock",
        "phonetic": "/ˈdedlɑːk/",
        "pos": "n.",
        "definition": "死锁；僵局",
        "example": "A deadlock occurred because Thread A and Thread B waited on each other's locks.",
        "memory_tip": "dead(死) + lock(锁) → 两个线程互相掐住对方的锁互不相让 → '死锁'。"
    },
    {
        "word": "serialization",
        "phonetic": "/ˌsɪriələˈzeɪʃn/",
        "pos": "n.",
        "definition": "序列化；串行化",
        "example": "JSON serialization transforms in-memory objects into a text payload.",
        "memory_tip": "serial(排成一串的) + ization → 将网状内存对象拉直成一列字节流 → '序列化'。"
    },
    {
        "word": "middleware",
        "phonetic": "/ˈmɪdlwer/",
        "pos": "n.",
        "definition": "中间件；中间拦截层",
        "example": "Authentication was implemented as a reusable HTTP middleware.",
        "memory_tip": "middle(中间) + ware(器物/部件) → 夹在请求与核心业务之间的管道层 → '中间件'。"
    },
    {
        "word": "refactor",
        "phonetic": "/ˌriːˈfæktər/",
        "pos": "v.",
        "definition": "重构 (在不改变外部行为的前提下优化代码内部结构)",
        "example": "We need to refactor this monolith file into smaller, testable modules.",
        "memory_tip": "re(再次) + factor(做/因素) → 重新梳理代码制造细节 → '重构'。"
    },
    {
        "word": "pipeline",
        "phonetic": "/ˈpaɪplaɪn/",
        "pos": "n.",
        "definition": "流水线；管道",
        "example": "The CI/CD pipeline runs unit tests automatically on every push.",
        "memory_tip": "pipe(管子) + line(线) → 多阶段任务依次流过的自动化管道 → '流水线'。"
    },
    {
        "word": "dependency",
        "phonetic": "/dɪˈpendənsi/",
        "pos": "n.",
        "definition": "依赖项；依赖关系",
        "example": "Managing third-party dependencies with a package manager avoids version conflicts.",
        "memory_tip": "depend(依靠) + ency → 代码运行时必须仰仗的外部组件 → '依赖项'。"
    },
    {
        "word": "overhead",
        "phonetic": "/ˈoʊvərhed/",
        "pos": "n.",
        "definition": "开销；额外支出 (如性能损耗、内存开销)",
        "example": "Virtual function table lookups introduce a tiny runtime overhead.",
        "memory_tip": "over(在上方) + head(头顶) → 顶在头上的额外负担与耗费 → '系统开销'。"
    },
    {
        "word": "truncate",
        "phonetic": "/ˈtrʌŋkeɪt/",
        "pos": "v.",
        "definition": "截断；删节；截短",
        "example": "The log message was truncated to 500 characters to save disk space.",
        "memory_tip": "trunk(树干) + ate → 像砍掉树冠只留树桩一样切除多余部分 → '截断'。"
    },
    {
        "word": "sandbox",
        "phonetic": "/ˈsændbɑːks/",
        "pos": "n.",
        "definition": "沙盒；隔离测试环境",
        "example": "Plugins are executed inside a secure sandbox to protect user privacy.",
        "memory_tip": "sand(沙) + box(盒子) → 小朋友在沙盒里随便折腾不会破坏房间 → '沙盒/安全隔离区'。"
    },
    {
        "word": "debounce",
        "phonetic": "/diːˈbaʊns/",
        "pos": "v./n.",
        "definition": "防抖 (短时间内连续触发只执行最后一次)",
        "example": "Debounce the input event so search API requests aren't fired on every keystroke.",
        "memory_tip": "de(去除) + bounce(弹跳/杂波) → 消除机械开关弹跳产生的毛刺 → '防抖'。"
    },
    {
        "word": "throttle",
        "phonetic": "/ˈθrɑːtl/",
        "pos": "v./n.",
        "definition": "节流；限流 (固定时间间隔内只执行一次)",
        "example": "Throttle the scroll listener to improve rendering framerate.",
        "memory_tip": "原意为掐住喉咙控制流量大小 → 限制执行频率 → '节流，限频'。"
    },
    {
        "word": "coroutine",
        "phonetic": "/koʊˈruːtiːn/",
        "pos": "n.",
        "definition": "协程；协同程序",
        "example": "C++20 coroutines allow writing asynchronous code in a linear style.",
        "memory_tip": "co(协同) + routine(例行程序) → 相互让出执行权通力协作的微型例程 → '协程'。"
    },
    {
        "word": "mutex",
        "phonetic": "/ˈmjuːteks/",
        "pos": "n.",
        "definition": "互斥锁 (Mutual Exclusion)",
        "example": "Always protect shared data structures with a mutex locker.",
        "memory_tip": "Mut(ual) + Ex(clusion) 的缩写 → '互斥锁'。"
    },
    {
        "word": "payload",
        "phonetic": "/ˈpeɪloʊd/",
        "pos": "n.",
        "definition": "有效载荷；载荷数据",
        "example": "The HTTP body carries the JSON payload for the create user request.",
        "memory_tip": "pay(付钱) + load(装载货物) → 真正赚钱的核心货物（去除包装包头后的真实内容） → '有效载荷'。"
    },
    {
        "word": "callback",
        "phonetic": "/ˈkɔːlbæk/",
        "pos": "n.",
        "definition": "回调函数",
        "example": "Pass a lambda callback to handle the asynchronous network response.",
        "memory_tip": "call(打电话) + back(回复) → 事情办完之后再回拨给我 → '回调函数'。"
    },
    {
        "word": "exception",
        "phonetic": "/ɪkˈsepʃn/",
        "pos": "n.",
        "definition": "异常；例外情况",
        "example": "Wrap the file operation in a try-catch block to handle IO exceptions.",
        "memory_tip": "except(除...之外) + ion → 落在常规逻辑之外的不测风云 → '异常'。"
    },
    {
        "word": "namespace",
        "phonetic": "/ˈneɪmspeɪs/",
        "pos": "n.",
        "definition": "命名空间",
        "example": "Using namespaces prevents global symbol collisions in C++ projects.",
        "memory_tip": "name(名字) + space(空间) → 划分名字归属的专属领地 → '命名空间'。"
    },
    {
        "word": "template",
        "phonetic": "/ˈtempleɪt/",
        "pos": "n.",
        "definition": "模板；样板",
        "example": "C++ templates enable type-agnostic generic programming.",
        "memory_tip": "用来浇筑不同类型金属模具的底胚 → '模板/泛型'。"
    },
    {
        "word": "pointer",
        "phonetic": "/ˈpɔɪntər/",
        "pos": "n.",
        "definition": "指针 (保存内存地址的变量)",
        "example": "Using smart pointers helps prevent dangling memory references.",
        "memory_tip": "point(指向) + er → 指着某块内存地址的手指 → '指针'。"
    },
    {
        "word": "semantic",
        "phonetic": "/sɪˈmæntɪk/",
        "pos": "adj.",
        "definition": "语义的；语义学的",
        "example": "Semantic versioning uses Major.Minor.Patch to denote breaking changes.",
        "memory_tip": "seman(意义/标志) + tic → 与内在含义密切相关的 → '语义的'。"
    }
]

def main():
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    vocab_dir = os.path.join(base_dir, "res", "vocab")
    os.makedirs(vocab_dir, exist_ok=True)

    upgrade_path = os.path.join(vocab_dir, "vocab_upgrade_exam.json")
    with open(upgrade_path, "w", encoding="utf-8") as f:
        json.dump(UPGRADE_EXAM_WORDS, f, ensure_ascii=False, indent=2)
    print(f"Generated {len(UPGRADE_EXAM_WORDS)} upgrade exam words at: {upgrade_path}")

    prog_path = os.path.join(vocab_dir, "vocab_programming.json")
    with open(prog_path, "w", encoding="utf-8") as f:
        json.dump(PROGRAMMING_WORDS, f, ensure_ascii=False, indent=2)
    print(f"Generated {len(PROGRAMMING_WORDS)} programming words at: {prog_path}")

if __name__ == "__main__":
    main()

