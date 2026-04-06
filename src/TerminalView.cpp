#include "TerminalView.h"
#include "DebugTrace.h"

#include <QAbstractSlider>
#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFocusEvent>
#include <QFont>
#include <QFontMetrics>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextOption>

namespace { constexpr QChar kEscape('\x1b'); }

TerminalView::TerminalView(QWidget *parent) : QTextEdit(parent)
{
    setReadOnly(true);
    setUndoRedoEnabled(false);
    setAcceptRichText(false);
    setFocusPolicy(Qt::StrongFocus);
    setTabChangesFocus(false);
    setAttribute(Qt::WA_InputMethodEnabled, true);
    setCursorWidth(0);
    setLineWrapMode(QTextEdit::NoWrap);
    setWordWrapMode(QTextOption::NoWrap);
    setTextInteractionFlags(Qt::TextSelectableByMouse);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    restoreBaseFormats();
    resizeBuffers(m_rows, m_columns);
    clearBuffer(m_mainBuffer);
    clearBuffer(m_altBuffer);
    renderScreen();
}

void TerminalView::appendRemoteText(const QString &text, bool isErrorStream)
{
    if (!text.isEmpty()) {
        wjsshTrace(QStringLiteral("TerminalView::appendRemoteText begin len=%1 stderr=%2 alt=%3")
                       .arg(text.size())
                       .arg(isErrorStream)
                       .arg(m_altScreenEnabled));
        processRemoteText(text, isErrorStream ? m_errorRemoteFormat : m_defaultRemoteFormat);
        m_forceScrollToBottom = true;
        wjsshTrace(QStringLiteral("TerminalView::appendRemoteText before render rows=%1 cols=%2 scrollback=%3 alt=%4")
                       .arg(m_rows)
                       .arg(m_columns)
                       .arg(m_scrollback.size())
                       .arg(m_altScreenEnabled));
        renderScreen();
        wjsshTrace(QStringLiteral("TerminalView::appendRemoteText end"));
    }
}

void TerminalView::appendLocalMessage(const QString &text)
{
    if (text.isEmpty()) return;
    wjsshTrace(QStringLiteral("TerminalView::appendLocalMessage begin len=%1 alt=%2").arg(text.size()).arg(m_altScreenEnabled));
    if (m_altScreenEnabled) setAlternateScreenEnabled(false);
    appendPlainTextToBuffer(text, m_localFormat);
    m_forceScrollToBottom = true;
    wjsshTrace(QStringLiteral("TerminalView::appendLocalMessage before render rows=%1 cols=%2 scrollback=%3")
                   .arg(m_rows)
                   .arg(m_columns)
                   .arg(m_scrollback.size()));
    renderScreen();
    wjsshTrace(QStringLiteral("TerminalView::appendLocalMessage end"));
}

void TerminalView::resetTerminal()
{
    m_pendingEscape.clear();
    m_scrollback.clear();
    m_altScreenEnabled = false;
    clearBuffer(m_mainBuffer);
    clearBuffer(m_altBuffer);
    m_forceScrollToBottom = true;
    renderScreen();
}

int TerminalView::terminalColumns() const { return m_columns; }
int TerminalView::terminalRows() const { return m_rows; }

bool TerminalView::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->matches(QKeySequence::Paste)) {
            event->accept();
            return true;
        }
        if (keyEvent->matches(QKeySequence::Copy) && textCursor().hasSelection()) {
            return QTextEdit::event(event);
        }

        const bool isTerminalNavigationKey =
            keyEvent->key() == Qt::Key_Tab
            || keyEvent->key() == Qt::Key_Backtab
            || keyEvent->key() == Qt::Key_Return
            || keyEvent->key() == Qt::Key_Enter
            || keyEvent->key() == Qt::Key_Backspace
            || keyEvent->key() == Qt::Key_Delete
            || keyEvent->key() == Qt::Key_Left
            || keyEvent->key() == Qt::Key_Right
            || keyEvent->key() == Qt::Key_Up
            || keyEvent->key() == Qt::Key_Down
            || keyEvent->key() == Qt::Key_Home
            || keyEvent->key() == Qt::Key_End
            || keyEvent->key() == Qt::Key_PageUp
            || keyEvent->key() == Qt::Key_PageDown;
        const bool isTerminalCtrlKey =
            keyEvent->modifiers() == Qt::ControlModifier
            && keyEvent->key() >= Qt::Key_A
            && keyEvent->key() <= Qt::Key_Z;
        if (isTerminalNavigationKey || isTerminalCtrlKey) {
            event->accept();
            return true;
        }
    }
    return QTextEdit::event(event);
}

bool TerminalView::focusNextPrevChild(bool next)
{
    Q_UNUSED(next);
    return false;
}

QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImCursorRectangle:
        return cursorRect(textCursor());
    case Qt::ImFont:
        return font();
    case Qt::ImHints:
        return static_cast<int>(Qt::ImhNoPredictiveText);
    default:
        break;
    }
    return QTextEdit::inputMethodQuery(query);
}

void TerminalView::inputMethodEvent(QInputMethodEvent *event)
{
    if (event == nullptr) {
        return;
    }

    const QString commitText = event->commitString();
    if (!commitText.isEmpty()) {
        emit rawInput(commitText.toUtf8());
    }
    event->accept();
}

void TerminalView::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Copy) && textCursor().hasSelection()) { QTextEdit::keyPressEvent(event); return; }
    if (event->matches(QKeySequence::Paste)) { const QString text = QApplication::clipboard()->text(); if (!text.isEmpty()) emit rawInput(text.toUtf8()); return; }
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_C) { if (textCursor().hasSelection()) QTextEdit::keyPressEvent(event); else emit interruptRequested(); return; }
    if (event->modifiers() == Qt::ControlModifier) {
        switch (event->key()) {
        case Qt::Key_A: emit rawInput(QByteArray("\x01")); return;
        case Qt::Key_D: emit rawInput(QByteArray("\x04")); return;
        case Qt::Key_E: emit rawInput(QByteArray("\x05")); return;
        case Qt::Key_K: emit rawInput(QByteArray("\x0b")); return;
        case Qt::Key_L: emit rawInput(QByteArray("\x0c")); return;
        case Qt::Key_R: emit rawInput(QByteArray("\x12")); return;
        case Qt::Key_U: emit rawInput(QByteArray("\x15")); return;
        case Qt::Key_W: emit rawInput(QByteArray("\x17")); return;
        case Qt::Key_Z: emit rawInput(QByteArray("\x1a")); return;
        default: break;
        }
    }
    switch (event->key()) {
    case Qt::Key_Return: case Qt::Key_Enter: emit rawInput(QByteArray("\r")); return;
    case Qt::Key_Backspace: emit rawInput(QByteArray("\x7f")); return;
    case Qt::Key_Delete: emit rawInput(QByteArray("\x1b[3~")); return;
    case Qt::Key_Tab: emit rawInput(QByteArray("\t")); return;
    case Qt::Key_Backtab: emit rawInput(QByteArray("\x1b[Z")); return;
    case Qt::Key_Left: emit rawInput(QByteArray("\x1b[D")); return;
    case Qt::Key_Right: emit rawInput(QByteArray("\x1b[C")); return;
    case Qt::Key_Up: emit rawInput(QByteArray("\x1b[A")); return;
    case Qt::Key_Down: emit rawInput(QByteArray("\x1b[B")); return;
    case Qt::Key_Home: emit rawInput(QByteArray("\x1b[H")); return;
    case Qt::Key_End: emit rawInput(QByteArray("\x1b[F")); return;
    case Qt::Key_PageUp: verticalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepSub); return;
    case Qt::Key_PageDown: verticalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepAdd); return;
    default: break;
    }
    if (!(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        const QString text = event->text();
        if (!text.isEmpty()) { emit rawInput(text.toUtf8()); return; }
    }
    QTextEdit::keyPressEvent(event);
}

void TerminalView::resizeEvent(QResizeEvent *event)
{
    QTextEdit::resizeEvent(event);
    wjsshTrace(QStringLiteral("TerminalView::resizeEvent size=%1x%2").arg(event->size().width()).arg(event->size().height()));
    recalculateGridMetrics();
}

void TerminalView::showEvent(QShowEvent *event)
{
    QTextEdit::showEvent(event);
    wjsshTrace(QStringLiteral("TerminalView::showEvent"));
    recalculateGridMetrics(true);
}

void TerminalView::focusInEvent(QFocusEvent *event)
{
    QTextEdit::focusInEvent(event);
    renderScreen();
}

void TerminalView::focusOutEvent(QFocusEvent *event)
{
    QTextEdit::focusOutEvent(event);
    renderScreen();
}

void TerminalView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !(event->modifiers() & Qt::ShiftModifier)) {
        setFocus(Qt::MouseFocusReason);
        renderScreen();
        event->accept();
        return;
    }
    QTextEdit::mousePressEvent(event);
}

void TerminalView::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->modifiers() & Qt::ShiftModifier)) {
        event->accept();
        return;
    }
    QTextEdit::mouseMoveEvent(event);
}

void TerminalView::mouseReleaseEvent(QMouseEvent *event)
{
    if (!(event->modifiers() & Qt::ShiftModifier)) {
        event->accept();
        return;
    }
    QTextEdit::mouseReleaseEvent(event);
}

void TerminalView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!(event->modifiers() & Qt::ShiftModifier)) {
        setFocus(Qt::MouseFocusReason);
        renderScreen();
        event->accept();
        return;
    }
    QTextEdit::mouseDoubleClickEvent(event);
}

void TerminalView::restoreBaseFormats()
{
    m_defaultRemoteFormat = makeBaseFormat(QColor("#E6EDF3"), QColor("#050607"));
    m_errorRemoteFormat = makeBaseFormat(QColor("#FF9B7A"), QColor("#050607"));
    m_localFormat = makeBaseFormat(QColor("#8FB7FF"), QColor("#050607"), true);
    m_mainBuffer.currentFormat = m_defaultRemoteFormat; m_mainBuffer.savedFormat = m_defaultRemoteFormat;
    m_altBuffer.currentFormat = m_defaultRemoteFormat; m_altBuffer.savedFormat = m_defaultRemoteFormat;
}

void TerminalView::clearBuffer(ScreenBuffer &buffer)
{
    buffer.lines = QVector<Line>(m_rows, blankLine());
    buffer.cursorRow = buffer.cursorCol = buffer.savedRow = buffer.savedCol = 0;
    buffer.currentFormat = buffer.savedFormat = m_defaultRemoteFormat;
    buffer.scrollTop = 0; buffer.scrollBottom = qMax(0, m_rows - 1);
    buffer.decSpecialGraphics = false; buffer.wrapEnabled = true;
}

void TerminalView::resizeBuffers(int rows, int columns)
{
    auto resizeOne = [this, rows, columns](ScreenBuffer &buffer) {
        const int previousRows = buffer.lines.size();
        const int previousScrollTop = buffer.scrollTop;
        const int previousScrollBottom = buffer.scrollBottom;
        const bool hadFullScrollRegion =
            previousRows == 0
            || (previousScrollTop == 0 && previousScrollBottom >= qMax(0, previousRows - 1));
        QVector<Line> resized(rows, Line(columns, blankCell()));
        for (int r = 0; r < qMin(rows, buffer.lines.size()); ++r)
            for (int c = 0; c < qMin(columns, buffer.lines[r].size()); ++c)
                resized[r][c] = buffer.lines[r][c];
        buffer.lines = resized;
        buffer.cursorRow = qBound(0, buffer.cursorRow, qMax(0, rows - 1));
        buffer.cursorCol = qBound(0, buffer.cursorCol, qMax(0, columns - 1));
        buffer.savedRow = qBound(0, buffer.savedRow, qMax(0, rows - 1));
        buffer.savedCol = qBound(0, buffer.savedCol, qMax(0, columns - 1));
        if (hadFullScrollRegion) {
            buffer.scrollTop = 0;
            buffer.scrollBottom = qMax(0, rows - 1);
        } else {
            buffer.scrollTop = qBound(0, previousScrollTop, qMax(0, rows - 1));
            buffer.scrollBottom = qBound(buffer.scrollTop, previousScrollBottom, qMax(0, rows - 1));
        }
    };
    m_rows = rows; m_columns = columns; resizeOne(m_mainBuffer); resizeOne(m_altBuffer);
}

void TerminalView::recalculateGridMetrics(bool forceEmit)
{
    const QFontMetrics metrics(font());
    const int columns = qMax(40, qMax(160, viewport()->width() - 14) / qMax(1, metrics.horizontalAdvance(QLatin1Char('M'))));
    const int rows = qMax(10, qMax(80, viewport()->height() - 10) / qMax(1, metrics.lineSpacing()));
    const bool changed = rows != m_rows || columns != m_columns;
    wjsshTrace(QStringLiteral("TerminalView::recalculateGridMetrics force=%1 viewport=%2x%3 rows=%4 cols=%5 changed=%6")
                   .arg(forceEmit)
                   .arg(viewport()->width())
                   .arg(viewport()->height())
                   .arg(rows)
                   .arg(columns)
                   .arg(changed));
    if (changed) { resizeBuffers(rows, columns); renderScreen(); }
    if (forceEmit || changed || rows != m_lastEmittedRows || columns != m_lastEmittedColumns) {
        m_lastEmittedRows = rows; m_lastEmittedColumns = columns; emit terminalSizeChanged(columns, rows);
    }
}

int TerminalView::visibleMainBufferRows() const
{
    int lastVisibleRow = -1;
    for (int row = m_mainBuffer.lines.size() - 1; row >= 0; --row) {
        if (lineHasVisibleContent(m_mainBuffer.lines[row])) {
            lastVisibleRow = row;
            break;
        }
    }
    return qMax(1, qMax(m_mainBuffer.cursorRow + 1, lastVisibleRow + 1));
}

void TerminalView::renderScreen()
{
    if (m_renderInProgress) {
        m_renderPending = true;
        wjsshTrace(QStringLiteral("TerminalView::renderScreen skipped because render is already in progress"));
        return;
    }

    m_renderInProgress = true;
    auto *bar = verticalScrollBar();
    const int previousValue = bar->value();
    const bool stickToBottom = m_forceScrollToBottom || previousValue >= bar->maximum() - 8 || m_altScreenEnabled;
    const int cursorLineOffset = m_altScreenEnabled ? 0 : m_scrollback.size();
    const int cursorLine = cursorLineOffset + activeBuffer().cursorRow;
    const int cursorColumn = qBound(0, activeBuffer().cursorCol, qMax(0, m_columns - 1));
    const bool showCursor = hasFocus();
    const int totalLines = m_altScreenEnabled ? m_rows : (m_scrollback.size() + visibleMainBufferRows());
    wjsshTrace(QStringLiteral("TerminalView::renderScreen begin totalLines=%1 cursor=%2,%3 scrollback=%4 alt=%5")
                   .arg(totalLines)
                   .arg(cursorLine)
                   .arg(cursorColumn)
                   .arg(m_scrollback.size())
                   .arg(m_altScreenEnabled));

    document()->clear();
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    for (int renderRow = 0; renderRow < totalLines; ++renderRow) {
        const Line *line = m_altScreenEnabled ? &m_altBuffer.lines[renderRow]
                           : (renderRow < m_scrollback.size() ? &m_scrollback[renderRow]
                                                              : &m_mainBuffer.lines[renderRow - m_scrollback.size()]);
        int lastColumn = line->size() - 1;
        while (lastColumn >= 0 && (*line)[lastColumn].ch == QChar(' ')
               && !(showCursor && renderRow == cursorLine && lastColumn < cursorColumn)) --lastColumn;
        if (showCursor && renderRow == cursorLine) lastColumn = qMax(lastColumn, cursorColumn);
        for (int column = 0; column <= lastColumn; ++column) {
            Cell cell = column < line->size() ? (*line)[column] : blankCell();
            QTextCharFormat format = cell.format;
            QChar ch = cell.ch.isNull() ? QChar(' ') : cell.ch;
            if (showCursor && renderRow == cursorLine && column == cursorColumn) format = invertedFormat(format);
            cursor.insertText(QString(ch), format);
        }
        if (renderRow + 1 < totalLines) cursor.insertBlock();
    }
    cursor.endEditBlock();
    wjsshTrace(QStringLiteral("TerminalView::renderScreen end edit block"));
    setTextCursor(cursor);
    wjsshTrace(QStringLiteral("TerminalView::renderScreen setTextCursor"));
    if (stickToBottom) {
        bar->setValue(bar->maximum());
    } else {
        bar->setValue(qMin(previousValue, bar->maximum()));
    }
    wjsshTrace(QStringLiteral("TerminalView::renderScreen end"));

    m_renderInProgress = false;
    m_forceScrollToBottom = false;
    if (m_renderPending) {
        m_renderPending = false;
        wjsshTrace(QStringLiteral("TerminalView::renderScreen flushing pending render"));
        renderScreen();
    }
}

bool TerminalView::lineHasVisibleContent(const Line &line)
{
    for (const Cell &cell : line) {
        if (!cell.ch.isNull() && cell.ch != QChar(' ')) {
            return true;
        }
    }
    return false;
}

void TerminalView::processRemoteText(const QString &text, const QTextCharFormat &defaultFormat)
{
    ScreenBuffer &buffer = activeBuffer();
    wjsshTrace(QStringLiteral("TerminalView::processRemoteText begin len=%1 cursor=%2,%3")
                   .arg(text.size())
                   .arg(buffer.cursorRow)
                   .arg(buffer.cursorCol));
    if (formatsMatch(buffer.currentFormat, m_defaultRemoteFormat) || formatsMatch(buffer.currentFormat, m_errorRemoteFormat))
        buffer.currentFormat = defaultFormat;
    QString input = m_pendingEscape + text; m_pendingEscape.clear();
    for (int i = 0; i < input.size();) {
        const QChar ch = input[i];
        if (ch == kEscape) {
            if (i + 1 >= input.size()) { m_pendingEscape = input.mid(i); break; }
            if (input[i + 1] == QChar('[')) {
                int end = i + 2; while (end < input.size()) { const ushort code = input[end].unicode(); if (code >= 0x40 && code <= 0x7E) break; ++end; }
                if (end >= input.size()) { m_pendingEscape = input.mid(i); break; }
                handleCsiSequence(QStringView(input).mid(i, end - i + 1), defaultFormat); i = end + 1; continue;
            }
            if (input[i + 1] == QChar(']')) {
                const int bell = input.indexOf(QChar('\x07'), i + 2);
                const int st = input.indexOf(QStringLiteral("\x1b\\"), i + 2);
                const int end = (bell >= 0 && (st < 0 || bell < st)) ? bell + 1 : (st >= 0 ? st + 2 : -1);
                if (end < 0) { m_pendingEscape = input.mid(i); break; }
                i = end; continue;
            }
            if (input[i + 1] == QChar('(') || input[i + 1] == QChar(')')) {
                if (i + 2 >= input.size()) { m_pendingEscape = input.mid(i); break; }
                buffer.decSpecialGraphics = input[i + 2] == QChar('0'); i += 3; continue;
            }
            handleEscapeSequence(QStringView(input).mid(i, 2), defaultFormat); i += 2; continue;
        }
        if (ch == QChar('\n')) { lineFeed(); ++i; continue; }
        if (ch == QChar('\r')) { carriageReturn(); ++i; continue; }
        if (ch == QChar('\b')) { backspace(); ++i; continue; }
        if (ch == QChar('\t')) { tab(); ++i; continue; }
        if (ch.unicode() < 0x20) { ++i; continue; }
        writeCharacter(buffer.decSpecialGraphics ? mapDecSpecialGraphics(ch) : ch, buffer.currentFormat);
        ++i;
    }
    wjsshTrace(QStringLiteral("TerminalView::processRemoteText end cursor=%1,%2 alt=%3 scrollback=%4 scrollRegion=%5-%6")
                   .arg(buffer.cursorRow)
                   .arg(buffer.cursorCol)
                   .arg(m_altScreenEnabled)
                   .arg(m_scrollback.size())
                   .arg(buffer.scrollTop)
                   .arg(buffer.scrollBottom));
    const auto summarizeLine = [](const Line &line) {
        QString text;
        text.reserve(line.size());
        for (const auto &cell : line) {
            text.append(cell.ch.isNull() ? QChar(' ') : cell.ch);
        }
        return text.trimmed();
    };
    const QString currentLine = summarizeLine(buffer.lines[buffer.cursorRow]).left(120);
    if (!currentLine.isEmpty()) {
        wjsshTrace(QStringLiteral("TerminalView::processRemoteText line[%1]=%2")
                       .arg(buffer.cursorRow)
                       .arg(currentLine));
    }
    QStringList visibleLines;
    for (int row = 0; row < qMin(m_rows, 6); ++row) {
        const QString lineText = summarizeLine(buffer.lines[row]).left(80);
        if (!lineText.isEmpty()) {
            visibleLines << QStringLiteral("%1:%2").arg(row).arg(lineText);
        }
    }
    if (!visibleLines.isEmpty()) {
        wjsshTrace(QStringLiteral("TerminalView::processRemoteText topLines=%1").arg(visibleLines.join(QStringLiteral(" | "))));
    }
}

void TerminalView::appendPlainTextToBuffer(const QString &text, const QTextCharFormat &format)
{
    ScreenBuffer &buffer = activeBuffer();
    const QTextCharFormat original = buffer.currentFormat;
    for (const QChar ch : text) {
        if (ch == QChar('\n')) { lineFeed(); carriageReturn(); continue; }
        if (ch == QChar('\r')) { carriageReturn(); continue; }
        if (ch == QChar('\t')) { tab(); continue; }
        writeCharacter(ch, format);
    }
    buffer.currentFormat = original;
}

void TerminalView::handleEscapeSequence(QStringView sequence, const QTextCharFormat &defaultFormat)
{
    Q_UNUSED(defaultFormat);
    if (sequence.size() < 2) return;
    ScreenBuffer &buffer = activeBuffer();
    const QChar op = sequence.at(1);
    if (op == QChar('7')) { buffer.savedRow = buffer.cursorRow; buffer.savedCol = buffer.cursorCol; buffer.savedFormat = buffer.currentFormat; return; }
    if (op == QChar('8')) { buffer.cursorRow = buffer.savedRow; buffer.cursorCol = buffer.savedCol; buffer.currentFormat = buffer.savedFormat; clampCursor(); return; }
    if (op == QChar('c')) { m_scrollback.clear(); m_altScreenEnabled = false; clearBuffer(m_mainBuffer); clearBuffer(m_altBuffer); return; }
    if (op == QChar('M')) { if (buffer.cursorRow == buffer.scrollTop) scrollDown(); else buffer.cursorRow = qMax(buffer.scrollTop, buffer.cursorRow - 1); }
}

void TerminalView::handleCsiSequence(QStringView sequence, const QTextCharFormat &defaultFormat)
{
    if (sequence.size() < 3) return;
    ScreenBuffer &buffer = activeBuffer();
    QStringView paramsView = sequence.mid(2, sequence.size() - 3);
    const bool privateMode = paramsView.startsWith(QChar('?'));
    if (privateMode) paramsView = paramsView.mid(1);
    const QList<int> params = parseParams(paramsView);
    switch (sequence.back().unicode()) {
    case 'A': moveCursor(buffer.cursorRow - paramOrDefault(params, 0, 1), buffer.cursorCol); break;
    case 'B': moveCursor(buffer.cursorRow + paramOrDefault(params, 0, 1), buffer.cursorCol); break;
    case 'C': moveCursor(buffer.cursorRow, buffer.cursorCol + paramOrDefault(params, 0, 1)); break;
    case 'D': moveCursor(buffer.cursorRow, buffer.cursorCol - paramOrDefault(params, 0, 1)); break;
    case 'E': moveCursor(buffer.cursorRow + paramOrDefault(params, 0, 1), 0); break;
    case 'F': moveCursor(buffer.cursorRow - paramOrDefault(params, 0, 1), 0); break;
    case 'G': moveCursor(buffer.cursorRow, paramOrDefault(params, 0, 1) - 1); break;
    case 'H': case 'f': moveCursor(paramOrDefault(params, 0, 1) - 1, paramOrDefault(params, 1, 1) - 1); break;
    case 'J': eraseInDisplay(paramOrDefault(params, 0, 0)); break;
    case 'K': eraseInLine(paramOrDefault(params, 0, 0)); break;
    case 'L': insertLines(paramOrDefault(params, 0, 1)); break;
    case 'M': deleteLines(paramOrDefault(params, 0, 1)); break;
    case 'P': deleteChars(paramOrDefault(params, 0, 1)); break;
    case 'S': scrollUp(paramOrDefault(params, 0, 1)); break;
    case 'T': scrollDown(paramOrDefault(params, 0, 1)); break;
    case 'X': eraseChars(paramOrDefault(params, 0, 1)); break;
    case '@': insertBlankChars(paramOrDefault(params, 0, 1)); break;
    case 'd': moveCursor(paramOrDefault(params, 0, 1) - 1, buffer.cursorCol); break;
    case 'm': applyAnsiCodes(params, defaultFormat); break;
    case 's': buffer.savedRow = buffer.cursorRow; buffer.savedCol = buffer.cursorCol; buffer.savedFormat = buffer.currentFormat; break;
    case 'u': buffer.cursorRow = buffer.savedRow; buffer.cursorCol = buffer.savedCol; buffer.currentFormat = buffer.savedFormat; clampCursor(); break;
    case 'r': {
        const int top = paramOrDefault(params, 0, 1) - 1;
        const int bottom = paramOrDefault(params, 1, m_rows) - 1;
        wjsshTrace(QStringLiteral("TerminalView::handleCsiSequence scrollRegion request top=%1 bottom=%2 rows=%3 private=%4")
                       .arg(top)
                       .arg(bottom)
                       .arg(m_rows)
                       .arg(privateMode));
        if (top >= 0 && top < bottom && bottom < m_rows) { buffer.scrollTop = top; buffer.scrollBottom = bottom; }
        else { buffer.scrollTop = 0; buffer.scrollBottom = qMax(0, m_rows - 1); }
        moveCursor(0, 0);
        break;
    }
    case 'h':
    case 'l':
        if (privateMode) {
            const bool enabled = sequence.back() == QChar('h');
            for (int value : params) {
                if (value == 1049 || value == 1047 || value == 47) setAlternateScreenEnabled(enabled);
                else if (value == 7) activeBuffer().wrapEnabled = enabled;
            }
        }
        break;
    default: break;
    }
}

void TerminalView::applyAnsiCodes(const QList<int> &codes, const QTextCharFormat &defaultFormat)
{
    ScreenBuffer &buffer = activeBuffer();
    const QList<int> normalized = codes.isEmpty() ? QList<int>{0} : codes;
    for (int i = 0; i < normalized.size(); ++i) {
        const int code = normalized.at(i);
        if (code == 0) { buffer.currentFormat = defaultFormat; continue; }
        if (code == 1) { buffer.currentFormat.setFontWeight(QFont::Bold); continue; }
        if (code == 3) { buffer.currentFormat.setFontItalic(true); continue; }
        if (code == 4) { buffer.currentFormat.setFontUnderline(true); continue; }
        if (code == 22) { buffer.currentFormat.setFontWeight(defaultFormat.fontWeight()); continue; }
        if (code == 23) { buffer.currentFormat.setFontItalic(defaultFormat.fontItalic()); continue; }
        if (code == 24) { buffer.currentFormat.setFontUnderline(defaultFormat.fontUnderline()); continue; }
        if (code == 39) { buffer.currentFormat.setForeground(defaultFormat.foreground()); continue; }
        if (code == 49) { buffer.currentFormat.setBackground(defaultFormat.background()); continue; }
        if ((code >= 30 && code <= 37) || (code >= 90 && code <= 97)) { const bool bright = code >= 90; buffer.currentFormat.setForeground(ansiStandardColor((bright ? code - 90 : code - 30), bright)); continue; }
        if ((code >= 40 && code <= 47) || (code >= 100 && code <= 107)) { const bool bright = code >= 100; buffer.currentFormat.setBackground(ansiStandardColor((bright ? code - 100 : code - 40), bright)); continue; }
        if ((code == 38 || code == 48) && i + 1 < normalized.size()) {
            const bool background = code == 48;
            const int mode = normalized.at(i + 1);
            if (mode == 5 && i + 2 < normalized.size()) {
                const QColor color = ansiIndexedColor(normalized.at(i + 2));
                if (background) buffer.currentFormat.setBackground(color); else buffer.currentFormat.setForeground(color);
                i += 2; continue;
            }
            if (mode == 2) {
                int componentIndex = i + 2;
                if (componentIndex + 3 < normalized.size() && normalized.at(componentIndex) == 0) {
                    ++componentIndex;
                }
                if (componentIndex + 2 >= normalized.size()) {
                    continue;
                }
                const QColor color(normalized.at(componentIndex),
                                   normalized.at(componentIndex + 1),
                                   normalized.at(componentIndex + 2));
                if (background) buffer.currentFormat.setBackground(color); else buffer.currentFormat.setForeground(color);
                i = componentIndex + 2;
                continue;
            }
        }
    }
}

void TerminalView::writeCharacter(QChar ch, const QTextCharFormat &format)
{
    ScreenBuffer &buffer = activeBuffer();
    clampCursor();
    if (buffer.cursorCol >= m_columns) {
        if (buffer.wrapEnabled) { carriageReturn(); lineFeed(); }
        else buffer.cursorCol = qMax(0, m_columns - 1);
    }
    Line &line = buffer.lines[buffer.cursorRow];
    line[buffer.cursorCol].ch = ch;
    line[buffer.cursorCol].format = format;
    if (buffer.cursorCol == m_columns - 1) {
        if (buffer.wrapEnabled) { carriageReturn(); lineFeed(); }
    } else {
        ++buffer.cursorCol;
    }
}

void TerminalView::lineFeed()
{
    ScreenBuffer &buffer = activeBuffer();
    const int bottom = qBound(0, buffer.scrollBottom, qMax(0, m_rows - 1));
    if (buffer.cursorRow >= bottom) scrollUp();
    else buffer.cursorRow = qMin(m_rows - 1, buffer.cursorRow + 1);
}

void TerminalView::carriageReturn() { activeBuffer().cursorCol = 0; }
void TerminalView::backspace() { activeBuffer().cursorCol = qMax(0, activeBuffer().cursorCol - 1); }

void TerminalView::tab()
{
    const int stop = ((activeBuffer().cursorCol / 8) + 1) * 8;
    while (activeBuffer().cursorCol < stop && activeBuffer().cursorCol < m_columns)
        writeCharacter(QChar(' '), activeBuffer().currentFormat);
}

void TerminalView::moveCursor(int row, int column)
{
    ScreenBuffer &buffer = activeBuffer();
    buffer.cursorRow = qBound(0, row, qMax(0, m_rows - 1));
    buffer.cursorCol = qBound(0, column, qMax(0, m_columns - 1));
}

void TerminalView::clampCursor()
{
    ScreenBuffer &buffer = activeBuffer();
    buffer.cursorRow = qBound(0, buffer.cursorRow, qMax(0, m_rows - 1));
    buffer.cursorCol = qBound(0, buffer.cursorCol, qMax(0, m_columns - 1));
}

void TerminalView::scrollUp(int count)
{
    ScreenBuffer &buffer = activeBuffer();
    const int top = qBound(0, buffer.scrollTop, qMax(0, m_rows - 1));
    const int bottom = qBound(top, buffer.scrollBottom, qMax(0, m_rows - 1));
    for (int i = 0; i < qMax(1, count); ++i) {
        if (!m_altScreenEnabled && top == 0 && bottom == m_rows - 1) pushScrollback(buffer.lines.at(top));
        buffer.lines.removeAt(top);
        buffer.lines.insert(bottom, blankLine());
    }
}

void TerminalView::scrollDown(int count)
{
    ScreenBuffer &buffer = activeBuffer();
    const int top = qBound(0, buffer.scrollTop, qMax(0, m_rows - 1));
    const int bottom = qBound(top, buffer.scrollBottom, qMax(0, m_rows - 1));
    for (int i = 0; i < qMax(1, count); ++i) {
        buffer.lines.removeAt(bottom);
        buffer.lines.insert(top, blankLine());
    }
}

void TerminalView::insertLines(int count)
{
    ScreenBuffer &buffer = activeBuffer();
    if (buffer.cursorRow < buffer.scrollTop || buffer.cursorRow > buffer.scrollBottom) return;
    for (int i = 0; i < qMin(count, buffer.scrollBottom - buffer.cursorRow + 1); ++i) {
        buffer.lines.removeAt(buffer.scrollBottom);
        buffer.lines.insert(buffer.cursorRow, blankLine());
    }
}

void TerminalView::deleteLines(int count)
{
    ScreenBuffer &buffer = activeBuffer();
    if (buffer.cursorRow < buffer.scrollTop || buffer.cursorRow > buffer.scrollBottom) return;
    for (int i = 0; i < qMin(count, buffer.scrollBottom - buffer.cursorRow + 1); ++i) {
        buffer.lines.removeAt(buffer.cursorRow);
        buffer.lines.insert(buffer.scrollBottom, blankLine());
    }
}

void TerminalView::insertBlankChars(int count)
{
    ScreenBuffer &buffer = activeBuffer();
    Line &line = buffer.lines[buffer.cursorRow];
    for (int i = 0; i < qMin(count, m_columns - buffer.cursorCol); ++i) { line.insert(buffer.cursorCol, blankCell()); line.removeLast(); }
}

void TerminalView::deleteChars(int count)
{
    ScreenBuffer &buffer = activeBuffer();
    Line &line = buffer.lines[buffer.cursorRow];
    for (int i = 0; i < qMin(count, m_columns - buffer.cursorCol); ++i) { line.removeAt(buffer.cursorCol); line.push_back(blankCell()); }
}

void TerminalView::eraseChars(int count)
{
    ScreenBuffer &buffer = activeBuffer();
    for (int i = 0; i < qMin(count, m_columns - buffer.cursorCol); ++i) buffer.lines[buffer.cursorRow][buffer.cursorCol + i] = blankCell();
}

void TerminalView::eraseInDisplay(int mode)
{
    ScreenBuffer &buffer = activeBuffer();
    if (mode == 2 || mode == 3) {
        for (Line &line : buffer.lines) line = blankLine();
        if (mode == 3) m_scrollback.clear();
        moveCursor(0, 0);
        return;
    }
    if (mode == 0) {
        eraseInLine(0);
        for (int row = buffer.cursorRow + 1; row < m_rows; ++row) buffer.lines[row] = blankLine();
        return;
    }
    if (mode == 1) {
        eraseInLine(1);
        for (int row = 0; row < buffer.cursorRow; ++row) buffer.lines[row] = blankLine();
    }
}

void TerminalView::eraseInLine(int mode)
{
    ScreenBuffer &buffer = activeBuffer();
    Line &line = buffer.lines[buffer.cursorRow];
    if (mode == 2) { line = blankLine(); return; }
    if (mode == 0) for (int c = buffer.cursorCol; c < m_columns; ++c) line[c] = blankCell();
    if (mode == 1) for (int c = 0; c <= buffer.cursorCol && c < m_columns; ++c) line[c] = blankCell();
}

void TerminalView::pushScrollback(const Line &line)
{
    m_scrollback.push_back(line);
    while (m_scrollback.size() > m_maxScrollbackLines) m_scrollback.removeAt(0);
}

void TerminalView::setAlternateScreenEnabled(bool enabled)
{
    if (enabled == m_altScreenEnabled) return;
    m_altScreenEnabled = enabled;
    if (enabled) clearBuffer(m_altBuffer);
}

TerminalView::ScreenBuffer &TerminalView::activeBuffer() { return m_altScreenEnabled ? m_altBuffer : m_mainBuffer; }
const TerminalView::ScreenBuffer &TerminalView::activeBuffer() const { return m_altScreenEnabled ? m_altBuffer : m_mainBuffer; }
TerminalView::Cell TerminalView::blankCell() const { return Cell {QChar(' '), m_defaultRemoteFormat}; }
TerminalView::Line TerminalView::blankLine() const { return Line(m_columns, blankCell()); }

bool TerminalView::formatsMatch(const QTextCharFormat &left, const QTextCharFormat &right)
{
    return left.foreground() == right.foreground()
           && left.background() == right.background()
           && left.fontWeight() == right.fontWeight()
           && left.fontItalic() == right.fontItalic()
           && left.fontUnderline() == right.fontUnderline();
}

QList<int> TerminalView::parseParams(QStringView params)
{
    if (params.isEmpty()) return {};
    QList<int> values;
    QString token;
    auto flushToken = [&values, &token]() {
        bool ok = false;
        values.push_back(token.isEmpty() ? 0 : token.toInt(&ok));
        if (!ok && !token.isEmpty()) {
            values.back() = 0;
        }
        token.clear();
    };
    for (const QChar ch : params) {
        if (ch == QChar(';') || ch == QChar(':')) {
            flushToken();
            continue;
        }
        token.append(ch);
    }
    flushToken();
    return values;
}

int TerminalView::paramOrDefault(const QList<int> &params, int index, int defaultValue)
{
    return (index < 0 || index >= params.size() || params.at(index) == 0) ? defaultValue : params.at(index);
}

QTextCharFormat TerminalView::makeBaseFormat(const QColor &foreground, const QColor &background, bool bold)
{
    QTextCharFormat format;
    format.setForeground(foreground);
    format.setBackground(background);
    format.setFontWeight(bold ? QFont::Bold : QFont::Normal);
    return format;
}

QTextCharFormat TerminalView::invertedFormat(const QTextCharFormat &format)
{
    QTextCharFormat copy = format;
    copy.setForeground(format.background().color().isValid() ? format.background() : QBrush(QColor("#050607")));
    copy.setBackground(format.foreground().color().isValid() ? format.foreground() : QBrush(QColor("#E6EDF3")));
    return copy;
}

QChar TerminalView::mapDecSpecialGraphics(QChar ch)
{
    switch (ch.unicode()) {
    case 'j': return QChar(0x2518); case 'k': return QChar(0x2510); case 'l': return QChar(0x250C);
    case 'm': return QChar(0x2514); case 'n': return QChar(0x253C); case 'q': return QChar(0x2500);
    case 't': return QChar(0x251C); case 'u': return QChar(0x2524); case 'v': return QChar(0x2534);
    case 'w': return QChar(0x252C); case 'x': return QChar(0x2502); default: return ch;
    }
}

QColor TerminalView::ansiIndexedColor(int index)
{
    if (index < 0) return QColor("#E6EDF3");
    if (index < 8) return ansiStandardColor(index, false);
    if (index < 16) return ansiStandardColor(index - 8, true);
    if (index < 232) {
        const int offset = index - 16, r = offset / 36, g = (offset / 6) % 6, b = offset % 6;
        const auto component = [](int value) { return value == 0 ? 0 : 55 + value * 40; };
        return QColor(component(r), component(g), component(b));
    }
    if (index < 256) { const int gray = 8 + (index - 232) * 10; return QColor(gray, gray, gray); }
    return QColor("#E6EDF3");
}

QColor TerminalView::ansiStandardColor(int index, bool bright)
{
    static const QColor normal[] = {QColor("#121417"), QColor("#D16969"), QColor("#7FB36B"), QColor("#D7BA7D"), QColor("#6EA8FE"), QColor("#C586C0"), QColor("#63C9D6"), QColor("#D7DBE0")};
    static const QColor vivid[] = {QColor("#5C6370"), QColor("#F28B82"), QColor("#98C379"), QColor("#E5C07B"), QColor("#7FB7FF"), QColor("#D7A6D8"), QColor("#7FDBE6"), QColor("#F5F7FA")};
    return bright ? vivid[qBound(0, index, 7)] : normal[qBound(0, index, 7)];
}
