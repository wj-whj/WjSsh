#pragma once

#include <QColor>
#include <QTextCharFormat>
#include <QTextEdit>
#include <QVector>

class QResizeEvent;
class QShowEvent;
class QFocusEvent;
class QMouseEvent;
class QEvent;
class QInputMethodEvent;

class TerminalView : public QTextEdit {
    Q_OBJECT

public:
    explicit TerminalView(QWidget *parent = nullptr);

    void appendRemoteText(const QString &text, bool isErrorStream = false);
    void appendLocalMessage(const QString &text);
    void resetTerminal();
    [[nodiscard]] int terminalColumns() const;
    [[nodiscard]] int terminalRows() const;

signals:
    void rawInput(const QByteArray &data);
    void interruptRequested();
    void terminalSizeChanged(int columns, int rows);

protected:
    bool event(QEvent *event) override;
    bool focusNextPrevChild(bool next) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    struct Cell {
        QChar ch = QChar(' ');
        QTextCharFormat format;
    };

    using Line = QVector<Cell>;

    struct ScreenBuffer {
        QVector<Line> lines;
        int cursorRow = 0;
        int cursorCol = 0;
        int savedRow = 0;
        int savedCol = 0;
        QTextCharFormat currentFormat;
        QTextCharFormat savedFormat;
        int scrollTop = 0;
        int scrollBottom = 0;
        bool decSpecialGraphics = false;
        bool wrapEnabled = true;
    };

    void restoreBaseFormats();
    void clearBuffer(ScreenBuffer &buffer);
    void resizeBuffers(int rows, int columns);
    void recalculateGridMetrics(bool forceEmit = false);
    [[nodiscard]] int visibleMainBufferRows() const;
    void renderScreen();
    void processRemoteText(const QString &text, const QTextCharFormat &defaultFormat);
    void appendPlainTextToBuffer(const QString &text, const QTextCharFormat &format);
    void handleEscapeSequence(QStringView sequence, const QTextCharFormat &defaultFormat);
    void handleCsiSequence(QStringView sequence, const QTextCharFormat &defaultFormat);
    void applyAnsiCodes(const QList<int> &codes, const QTextCharFormat &defaultFormat);
    void writeCharacter(QChar ch, const QTextCharFormat &format);
    void lineFeed();
    void carriageReturn();
    void backspace();
    void tab();
    void moveCursor(int row, int column);
    void clampCursor();
    void scrollUp(int count = 1);
    void scrollDown(int count = 1);
    void insertLines(int count);
    void deleteLines(int count);
    void insertBlankChars(int count);
    void deleteChars(int count);
    void eraseChars(int count);
    void eraseInDisplay(int mode);
    void eraseInLine(int mode);
    void pushScrollback(const Line &line);
    void setAlternateScreenEnabled(bool enabled);
    [[nodiscard]] ScreenBuffer &activeBuffer();
    [[nodiscard]] const ScreenBuffer &activeBuffer() const;
    [[nodiscard]] Cell blankCell() const;
    [[nodiscard]] Line blankLine() const;
    static bool lineHasVisibleContent(const Line &line);

    static bool formatsMatch(const QTextCharFormat &left, const QTextCharFormat &right);
    static QList<int> parseParams(QStringView params);
    static int paramOrDefault(const QList<int> &params, int index, int defaultValue);
    static QTextCharFormat makeBaseFormat(const QColor &foreground,
                                          const QColor &background,
                                          bool bold = false);
    static QTextCharFormat invertedFormat(const QTextCharFormat &format);
    static QChar mapDecSpecialGraphics(QChar ch);
    static QColor ansiIndexedColor(int index);
    static QColor ansiStandardColor(int index, bool bright);

    ScreenBuffer m_mainBuffer;
    ScreenBuffer m_altBuffer;
    bool m_altScreenEnabled = false;
    int m_rows = 24;
    int m_columns = 80;
    int m_lastEmittedRows = 0;
    int m_lastEmittedColumns = 0;
    int m_maxScrollbackLines = 2000;
    QString m_pendingEscape;
    QVector<Line> m_scrollback;
    QTextCharFormat m_defaultRemoteFormat;
    QTextCharFormat m_errorRemoteFormat;
    QTextCharFormat m_localFormat;
    bool m_renderInProgress = false;
    bool m_renderPending = false;
    bool m_forceScrollToBottom = false;
};
