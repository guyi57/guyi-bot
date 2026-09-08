#include "PersonaManager.hpp"
#include "SettingsDb.hpp"
#include "SkillManager.hpp"
#include <QRegularExpression>

PersonaManager* PersonaManager::instance()
{
    static PersonaManager s_instance;
    return &s_instance;
}

PersonaManager::PersonaManager()
{
    initPersonas();
}

void PersonaManager::initPersonas()
{
    m_personas.clear();
    m_personaList.clear();

    PersonaDefinition catgirl;
    catgirl.id = "catgirl";
    catgirl.name = "🐱 傲娇猫娘";
    catgirl.description = "口嫌体正直的傲娇猫娘，说话带喵，代码报错时会吐槽，心里很在乎主人。";
    catgirl.toneSuffix = "喵~";
    catgirl.defaultSystemPrompt = 
        "你是一只生活在主人电脑桌面上的傲娇猫娘桌宠（名为 Shijima）。"
        "你口嫌体正直，表面傲娇毒舌，心里却非常在乎和依赖主人。"
        "说话结尾喜欢带'喵~'或'哼~'。遇到代码报错或问题时，先轻微吐槽主人笨手笨脚，然后再给出精辟而专业的解答。"
        "回答言简意赅，充满可爱的互动感，适合在桌面小气泡中展示。";
    m_personas.insert(catgirl.id, catgirl);
    m_personaList.append(catgirl);

    PersonaDefinition maid;
    maid.id = "maid";
    maid.name = "🎀 元气女仆";
    maid.description = "温柔贴心、充满元气的女仆桌宠，称呼用户为主人，时刻给予鼓励与支持。";
    maid.toneSuffix = "主人~";
    maid.defaultSystemPrompt = 
        "你是一位元气满满、忠诚贴心的女仆桌宠（名为 Shijima），称呼用户为'主人'。"
        "你性格温柔阳光，充满正能量，无条件支持和信任主人。"
        "在主人编写代码、工作或学习时提供温暖的陪伴、高效准确的技术指导与最贴心的关怀。"
        "回答精炼干脆，语气活泼亲切，适合在桌面小气泡中展示。";
    m_personas.insert(maid.id, maid);
    m_personaList.append(maid);

    PersonaDefinition architect;
    architect.id = "architect";
    architect.name = "💻 硬核架构师";
    architect.description = "严谨干练的资深架构师，崇尚极客精神、代码美学与性能极致优化。";
    architect.toneSuffix = "";
    architect.defaultSystemPrompt = 
        "你是一位严谨干练的资深全栈系统架构师桌宠（名为 Shijima）。"
        "你言简意赅、崇尚极客精神与代码美学。注重代码质量、设计模式、性能极致优化与高可用架构。"
        "用精炼犀利的技术语言为主人解决开发难题，直击问题本质，拒绝冗长废话。";
    m_personas.insert(architect.id, architect);
    m_personaList.append(architect);

    PersonaDefinition healing;
    healing.id = "healing";
    healing.name = "🍵 治愈小精灵";
    healing.description = "温柔软萌的治愈系小精灵，关怀主人健康，提醒劳逸结合，安抚焦虑。";
    healing.toneSuffix = "🌸";
    healing.defaultSystemPrompt = 
        "你是一只温柔软萌的治愈系桌面小精灵（名为 Shijima）。"
        "你说话轻声细语，总是能敏锐捕捉到主人的压力与疲惫，用温暖治愈的话语抚慰心灵。"
        "在主人忙碌时提醒劳逸结合、多喝水休息，用最温柔的声音给出解答。";
    m_personas.insert(healing.id, healing);
    m_personaList.append(healing);

    PersonaDefinition custom;
    custom.id = "custom";
    custom.name = "🛠️ 自定义人格";
    custom.description = "由用户完全自定义的独特人设与提示词。";
    custom.toneSuffix = "";
    custom.defaultSystemPrompt = "你是一个智能桌面宠物助手，回答简洁高效，适合在桌面小气泡中展示。";
    m_personas.insert(custom.id, custom);
    m_personaList.append(custom);
}

QList<PersonaDefinition> PersonaManager::allPersonas() const
{
    return m_personaList;
}

PersonaDefinition PersonaManager::getPersona(QString const& id) const
{
    if (m_personas.contains(id)) {
        return m_personas.value(id);
    }
    return m_personas.value("catgirl");
}

QString PersonaManager::activePersonaId() const
{
    return SettingsDb::instance()->get("persona.active_id", "catgirl");
}

void PersonaManager::setActivePersonaId(QString const& id)
{
    SettingsDb::instance()->set("persona.active_id", id);
}

QString PersonaManager::customPersonaPrompt() const
{
    return SettingsDb::instance()->get("persona.custom_prompt", 
        "你是一个智能桌面宠物助手，回答简洁高效，适合在桌面小气泡中展示。");
}

void PersonaManager::setCustomPersonaPrompt(QString const& prompt)
{
    SettingsDb::instance()->set("persona.custom_prompt", prompt);
}

QString PersonaManager::buildEffectiveSystemPrompt() const
{
    QString id = activePersonaId();
    PersonaDefinition p = getPersona(id);

    QString basePrompt = p.defaultSystemPrompt;
    if (id == "custom") {
        QString userCustom = customPersonaPrompt().trimmed();
        if (!userCustom.isEmpty()) {
            basePrompt = userCustom;
        }
    }

    QString actionProtocol = 
        "\n\n【桌宠动画动作协议】\n"
        "你可以根据当前语义和心情，在回复内容的最前面附带且仅附带一个动作指令标签（必须放在首位）：\n"
        "- [action:jump] (跳跃欢呼/兴奋)\n"
        "- [action:sit] (坐下思考/静止)\n"
        "- [action:crawl] (趴着/摸鱼)\n"
        "- [action:work] (敲键盘/工作中)\n"
        "- [action:celebrate] (撒花庆祝/大功告成)\n"
        "- [action:resist] (傲娇摔倒/抗拒/摇头)\n"
        "- [action:walk] (走动/巡视)\n"
        "示例：'[action:jump] 太棒了！这个 Bug 已经被我们彻底消灭啦喵~'\n\n"
        "【排版与格式规范（必须严格遵守）】\n"
        "桌面悬浮气泡阅读空间有限，无论回答天气、任务报告、还是日常问答，排版必须层级清晰、赏心悦目：\n"
        "1. 禁止将所有信息无换行挤在一长段！段落之间必须空一行（两个换行符）。\n"
        "2. 结构化信息（如天气预报、多步骤任务、数据概览）请使用标准 Markdown 规范组织：\n"
        "   - 主标题：如 '### 城市天气（今明后三天）'\n"
        "   - 地点/标签：如 '📍 成都 · 四川'\n"
        "   - 日期/分块：如 '📅 今天 · 9月4日（周四）'\n"
        "   - 列表项：必须换行使用 '- ' 或 '◦ ' 引导，项目名称加冒号（如 '- 天气：晴到多云'、'- 气温：23℃ ~ 29℃'）。\n"
        "   - 小贴士/注意事项：如 '💡 温馨小贴士：' 后紧跟列表项。\n"
        "3. 开头有招呼语，结尾可有简短关怀或总结，字句自然亲切。";

    QString skillExtensions = SkillManager::instance()->getSystemPromptExtensions();
    return basePrompt + skillExtensions + actionProtocol;
}

void PersonaManager::parseActionAndContent(QString const& rawOutput, QString &outAction, QString &outCleanText)
{
    outAction = "";
    outCleanText = rawOutput.trimmed();

    static QRegularExpression re(R"(^\[action:([a-zA-Z0-9_\-]+)\]\s*)");
    auto match = re.match(outCleanText);
    if (match.hasMatch()) {
        outAction = match.captured(1).toLower();
        outCleanText = outCleanText.mid(match.capturedLength()).trimmed();
    }
}

QString PersonaManager::renderStatusNarration(QString const& agentName, QString const& status, 
                                              QString const& task, QString const& details) const
{
    QString persona = activePersonaId();
    QString prefix = agentName.isEmpty() ? "Coding Agent" : agentName;
    QString mainText = task.isEmpty() ? details : task;
    if (mainText.isEmpty()) mainText = "任务执行中";

    if (persona == "catgirl") {
        if (status == "thinking") return QString("【%1】正在绞尽脑汁分析代码逻辑呢，主人别催喵~").arg(prefix);
        if (status == "working" || status == "coding") return QString("【%1】正在疯狂写代码：%2 🐾").arg(prefix, mainText);
        if (status == "need_approval") return QString("⚠️【%1】停下来了！有关键操作需要主人审批，快去看看喵！").arg(prefix);
        if (status == "finished") return QString("✨【%1】搞定啦！%2，还不快夸夸本喵~").arg(prefix, mainText);
        if (status == "error") return QString("❌【%1】翻车了！%2 笨蛋主人快去检查一下喵！").arg(prefix, mainText);
        if (status == "idle") return QString("【%1】目前处于空闲状态喵~").arg(prefix);
    } else if (persona == "maid") {
        if (status == "thinking") return QString("主人，【%1】正在专注构思解决方案中...").arg(prefix);
        if (status == "working" || status == "coding") return QString("主人加油！【%1】正在努力执行：%2 ✨").arg(prefix, mainText);
        if (status == "need_approval") return QString("🔔 主人，【%1】需要您的授权确认，请您过目哦~").arg(prefix);
        if (status == "finished") return QString("🎉 报告主人！【%1】已顺利完成：%2，主人辛苦啦！").arg(prefix, mainText);
        if (status == "error") return QString("⚠️ 主人，【%1】遇到了一点小错误：%2，请主人查看~").arg(prefix, mainText);
        if (status == "idle") return QString("主人，【%1】已就绪，随时听候您的吩咐~").arg(prefix);
    } else if (persona == "architect") {
        if (status == "thinking") return QString("[%1] 正在进行方案推演与 AST 静态分析...").arg(prefix);
        if (status == "working" || status == "coding") return QString("[%1] 正在执行任务流水线: %2").arg(prefix, mainText);
        if (status == "need_approval") return QString("[!] [%1] 触发阻断点: 正在等待人工 Review 审批").arg(prefix);
        if (status == "finished") return QString("[%1] 任务执行完毕: %2 [OK]").arg(prefix, mainText);
        if (status == "error") return QString("[%1] 任务异常中断: %2 [ERROR]").arg(prefix, mainText);
        if (status == "idle") return QString("[%1] 处于就绪状态 (Idle)").arg(prefix);
    } else if (persona == "healing") {
        if (status == "thinking") return QString("【%1】正在静静思考呢，主人先喝口水休息一下吧 🍵").arg(prefix);
        if (status == "working" || status == "coding") return QString("【%1】正在认真推进：%2 🌸").arg(prefix, mainText);
        if (status == "need_approval") return QString("【%1】在等待主人确认哦，慢慢来不着急~").arg(prefix);
        if (status == "finished") return QString("【%1】顺利完成啦！主人最棒了~ 🌸").arg(prefix, mainText);
        if (status == "error") return QString("【%1】遇到了一点波折，不要灰心，我们一起来看看吧~").arg(prefix, mainText);
        if (status == "idle") return QString("【%1】静静陪伴在主人身边 🌿").arg(prefix);
    }

    // Default / Custom
    if (status == "thinking") return QString("[%1] 正在思考分析...").arg(prefix);
    if (status == "working" || status == "coding") return QString("[%1] 正在执行: %2").arg(prefix, mainText);
    if (status == "need_approval") return QString("⚠️ [%1] 需要您的确认与审批").arg(prefix);
    if (status == "finished") return QString("✅ [%1] 任务完成: %2").arg(prefix, mainText);
    if (status == "error") return QString("❌ [%1] 执行失败: %2").arg(prefix, mainText);
    return QString("[%1] %2").arg(prefix, mainText);
}
