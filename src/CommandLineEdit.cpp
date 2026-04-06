#include "CommandLineEdit.h"

#include <QKeyEvent>

CommandLineEdit::CommandLineEdit(QWidget *parent)
    : QLineEdit(parent)
{
}

void CommandLineEdit::addHistoryEntry(const QString &command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    if (m_history.isEmpty() || m_history.constLast() != trimmed) {
        m_history.push_back(trimmed);
        while (m_history.size() > 200) {
            m_history.removeFirst();
        }
    }

    clearNavigationState();
}

void CommandLineEdit::clearNavigationState()
{
    m_historyIndex = -1;
    m_draftText.clear();
}

void CommandLineEdit::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Up && !m_history.isEmpty()) {
        if (m_historyIndex == -1) {
            m_draftText = text();
            m_historyIndex = m_history.size() - 1;
        } else if (m_historyIndex > 0) {
            --m_historyIndex;
        }
        showHistoryEntry(m_historyIndex);
        return;
    }

    if (event->key() == Qt::Key_Down && m_historyIndex != -1) {
        if (m_historyIndex < m_history.size() - 1) {
            ++m_historyIndex;
            showHistoryEntry(m_historyIndex);
        } else {
            m_historyIndex = -1;
            setText(m_draftText);
            setCursorPosition(text().size());
        }
        return;
    }

    if (m_historyIndex != -1) {
        m_historyIndex = -1;
        m_draftText = text();
    }

    QLineEdit::keyPressEvent(event);
}

void CommandLineEdit::showHistoryEntry(int index)
{
    if (index < 0 || index >= m_history.size()) {
        return;
    }

    setText(m_history.at(index));
    setCursorPosition(text().size());
}

