#pragma once

#include <QString>
#include <QList>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>

struct PersonaDefinition {
    QString id;                 // e.g. "catgirl", "maid", "architect", "healing", "custom"
    QString name;               // e.g. "🐱 傲娇猫娘"
    QString description;        // e.g. "口嫌体正直的傲娇猫娘，说话带喵，代码报错时会吐槽"
    QString defaultSystemPrompt;// 预设人设 Prompt
    QString toneSuffix;         // e.g. "喵~"
};

class PersonaManager {
public:
    static PersonaManager* instance();

    // 人格定义检索
    QList<PersonaDefinition> allPersonas() const;
    PersonaDefinition getPersona(QString const& id) const;

    // 当前选中的人格与提示词
    QString activePersonaId() const;
    void setActivePersonaId(QString const& id);
    PersonaDefinition currentPersona() const { return getPersona(activePersonaId()); }

    QString customPersonaPrompt() const;
    void setCustomPersonaPrompt(QString const& prompt);

    // 获取用于 LLM 的完整 System Prompt（注入角色人设与动作协议）
    QString buildEffectiveSystemPrompt() const;

    // 解析回复文本中的情感/动作标签（例如：[action:jump] [action:celebrate] [action:sit]）
    static void parseActionAndContent(QString const& rawOutput, QString &outAction, QString &outCleanText);

    // 本地 0 Token 状态感知播报模板（根据不同人格生成独特的口语化播报）
    QString renderStatusNarration(QString const& agentName, QString const& status, 
                                  QString const& task, QString const& details) const;

private:
    PersonaManager();
    void initPersonas();

    QMap<QString, PersonaDefinition> m_personas;
    QList<PersonaDefinition> m_personaList;
};
