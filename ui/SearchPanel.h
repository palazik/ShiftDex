// ShiftDex - unpack, edit and repack APK and JAR files
// Copyright (C) 2026 palaziks
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAtomicInt>
#include <QList>
#include <QThread>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

struct SearchHit {
    QString path; // relative to the project
    int line = 0;
    int column = 0;
    QString text;
};
Q_DECLARE_METATYPE(SearchHit)

// Runs the native search (native/sdx_search.c) off the UI thread.
class SearchWorker : public QThread {
    Q_OBJECT
public:
    SearchWorker(const QString &root, const QString &pattern, bool ignoreCase, bool regex, bool namesOnly,
                 QObject *parent = nullptr);
    void stop() { m_stop.storeRelease(1); }
    bool stopped() const { return m_stop.loadAcquire() != 0; }
    void push(const SearchHit &hit);

signals:
    void hits(const QList<SearchHit> &batch);
    void done(int code, int files, int matches, bool truncated);

protected:
    void run() override;

private:
    void flush();

    QString m_root, m_pattern;
    bool m_ignoreCase, m_regex, m_namesOnly;
    QAtomicInt m_stop;
    QList<SearchHit> m_batch;
};

// Side panel: search file contents or file names across the project.
class SearchPanel : public QWidget {
    Q_OBJECT
public:
    explicit SearchPanel(QWidget *parent = nullptr);
    ~SearchPanel() override;

    void setRoot(const QString &root);
    void activate(bool fileNames);

signals:
    void openRequested(const QString &path, int line, int column);

private:
    void start();
    void cancel();
    void addHits(const QList<SearchHit> &batch);

    QString m_root;
    QLineEdit *m_input;
    QComboBox *m_mode;
    QCheckBox *m_case;
    QCheckBox *m_regex;
    QLabel *m_status;
    QTreeWidget *m_results;
    QTimer *m_debounce;
    SearchWorker *m_worker = nullptr;
    QHash<QString, QTreeWidgetItem *> m_fileItems;
};
