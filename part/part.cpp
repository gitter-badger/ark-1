/*
 * ark -- archiver for the KDE project
 *
 * Copyright (C) 2007 Henrique Pinto <henrique.pinto@kdemail.net>
 * Copyright (C) 2008-2009 Harald Hvaal <haraldhv@stud.ntnu.no>
 * Copyright (C) 2009 Raphael Kubo da Costa <kubito@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include "part.h"
#include "archivemodel.h"
#include "archiveview.h"
#include "arkviewer.h"
#include "dnddbusinterface.h"
#include "infopanel.h"
#include "jobtracker.h"
#include "kerfuffle/archive.h"
#include "kerfuffle/extractiondialog.h"
#include "kerfuffle/jobs.h"
#include "kerfuffle/settings.h"

#include <KAboutData>
#include <KAction>
#include <KActionCollection>
#include <KApplication>
#include <KConfigGroup>
#include <KDebug>
#include <KFileDialog>
#include <KIO/Job>
#include <KIO/NetAccess>
#include <KIcon>
#include <KInputDialog>
#include <KMessageBox>
#include <KPluginFactory>
#include <KRun>
#include <KSelectAction>
#include <KStandardDirs>
#include <KTempDir>
#include <KToggleAction>

#include <QAction>
#include <QCursor>
#include <QHeaderView>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QtDBus/QtDBus>

using namespace Kerfuffle;

K_PLUGIN_FACTORY(Factory, registerPlugin<Ark::Part>();)
K_EXPORT_PLUGIN(Factory("ark"))

namespace Ark
{

Part::Part(QWidget *parentWidget, QObject *parent, const QVariantList& args)
        : KParts::ReadWritePart(parent),
          m_model(new ArchiveModel(this)),
          m_splitter(0),
          m_previewDir(0),
          m_busy(false),
          m_jobTracker(0)
{
    Q_UNUSED(args)
    setComponentData(Factory::componentData(), false);

    m_splitter = new QSplitter(Qt::Horizontal, parentWidget);
    setWidget(m_splitter);

    m_view = new ArchiveView(m_splitter);
    m_infoPanel = new InfoPanel(m_model, m_splitter);

    QList<int> splitterSizes = ArkSettings::splitterSizes();
    if (splitterSizes.isEmpty()) {
        splitterSizes.append(200);
        splitterSizes.append(100);
    }
    m_splitter->setSizes(splitterSizes);

    setupView();
    setupActions();

    connect(m_splitter, SIGNAL(splitterMoved(int, int)), SLOT(slotUpdateSplitterSizes()));

    connect(m_model, SIGNAL(loadingStarted()),
            this, SLOT(slotLoadingStarted()));
    connect(m_model, SIGNAL(loadingFinished(KJob *)),
            this, SLOT(slotLoadingFinished(KJob *)));
    connect(m_model, SIGNAL(droppedFiles(const QStringList&, const QString&)),
            this, SLOT(slotAddFiles(const QStringList&, const QString&)));
    connect(m_model, SIGNAL(error(const QString&, const QString&)),
            this, SLOT(slotError(const QString&, const QString&)));

    connect(this, SIGNAL(busy()),
            this, SLOT(setBusyGui()));
    connect(this, SIGNAL(ready()),
            this, SLOT(setReadyGui()));
    connect(this, SIGNAL(completed()),
            this, SLOT(setFileNameFromArchive()));

    m_statusBarExtension = new KParts::StatusBarExtension(this);

    new DndExtractAdaptor(this);
    QDBusConnection::sessionBus().registerObject("/DndExtract", this);

    setXMLFile("ark_part.rc");
}

Part::~Part()
{
    m_extractFilesAction->menu()->deleteLater();

    delete m_previewDir;
    m_previewDir = 0;
}

void Part::registerJob(KJob* job)
{
    if (!m_jobTracker) {
        m_jobTracker = new JobTracker();
        m_statusBarExtension->addStatusBarItem(m_jobTracker->widget(0), 0, true);
        m_jobTracker->widget(job)->show();
    }
    m_jobTracker->registerJob(job);

    //KIO::getJobTracker()->registerJob(job);
    emit busy();
    connect(job, SIGNAL(finished(KJob*)),
            this, SIGNAL(ready()));
}

void Part::extractSelectedFilesTo(const QString& localPath)
{
    kDebug() << "Extract to " << localPath;
    if (!m_model) return;

    if (m_view->selectionModel()->selectedRows().count() != 1) {
        m_view->selectionModel()->setCurrentIndex(m_view->currentIndex(), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    if (m_view->selectionModel()->selectedRows().count() != 1) return;

    QVariant internalRoot;
    kDebug() << "valid " << m_view->currentIndex().parent().isValid();
    if (m_view->currentIndex().parent().isValid())
        internalRoot = m_model->entryForIndex(m_view->currentIndex().parent()).value(InternalID);

    if (internalRoot.isNull()) {
        //we have the special case valid parent, but the parent does not
        //actually correspond to an item in the archive, but an automatically
        //created folder. for now, we will just use the filename of the node
        //instead, but for plugins that rely on a non-filename value as the
        //InternalId, this WILL break things. TODO find a solution
        internalRoot = m_model->entryForIndex(m_view->currentIndex().parent()).value(FileName);
    }

    QList<QVariant> files = selectedFilesWithChildren();
    if (files.isEmpty()) return;

    kDebug() << "selected files are " << files;
    Kerfuffle::ExtractionOptions options;
    options["PreservePaths"] = true;
    if (!internalRoot.isNull()) options["RootNode"] = internalRoot;

    ExtractJob *job = m_model->extractFiles(files, localPath, options);
    registerJob(job);

    connect(job, SIGNAL(result(KJob*)),
            this, SLOT(slotExtractionDone(KJob *)));

    job->start();
}

void Part::setupView()
{
    m_view->setModel(m_model);

    m_view->setSortingEnabled(true);

    connect(m_view->selectionModel(), SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)),
            this, SLOT(updateActions()));
    connect(m_view->selectionModel(), SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)),
            this, SLOT(selectionChanged()));

    //TODO: fix an actual eventhandler
    connect(m_view, SIGNAL(itemTriggered(const QModelIndex &)),
            this, SLOT(slotPreview(const QModelIndex &)));

    connect(m_model, SIGNAL(columnsInserted(const QModelIndex &, int, int)),
            this, SLOT(adjustColumns()));
}

void Part::setupActions()
{
    KToggleAction *showInfoPanelAction = new KToggleAction(i18nc("@action:inmenu", "Show information panel"), this);
    actionCollection()->addAction("show-infopanel", showInfoPanelAction);
    showInfoPanelAction->setChecked(m_splitter->sizes().at(1) > 0);
    connect(showInfoPanelAction, SIGNAL(triggered(bool)),
            this, SLOT(slotToggleInfoPanel(bool)));

    m_saveAsAction = KStandardAction::saveAs(this, SLOT(slotSaveAs()), actionCollection());

    m_previewAction = actionCollection()->addAction("preview");
    m_previewAction->setText(i18nc("to preview a file inside an archive", "Pre&view"));
    m_previewAction->setIcon(KIcon("document-preview-archive"));
    m_previewAction->setStatusTip(i18n("Click to preview the selected file"));
    connect(m_previewAction, SIGNAL(triggered(bool)),
            this, SLOT(slotPreview()));

    m_extractFilesAction = actionCollection()->addAction("extract");
    m_extractFilesAction->setText(i18n("E&xtract"));
    m_extractFilesAction->setIcon(KIcon("archive-extract"));
    m_extractFilesAction->setStatusTip(i18n("Click to open an extraction dialog, where you can choose to extract either all files or just the selected ones"));
    m_extractFilesAction->setShortcut(QString("Ctrl+E"));
    connect(m_extractFilesAction, SIGNAL(triggered(bool)),
            this, SLOT(slotExtractFiles()));

    m_addFilesAction = actionCollection()->addAction("add");
    m_addFilesAction->setIcon(KIcon("archive-insert"));
    m_addFilesAction->setText(i18n("Add &File..."));
    m_addFilesAction->setStatusTip(i18n("Click to add files to the archive"));
    connect(m_addFilesAction, SIGNAL(triggered(bool)),
            this, SLOT(slotAddFiles()));

    m_addDirAction = actionCollection()->addAction("add-dir");
    m_addDirAction->setIcon(KIcon("archive-insert-directory"));
    m_addDirAction->setText(i18n("Add Fo&lder..."));
    m_addDirAction->setStatusTip(i18n("Click to add a folder to the archive"));
    connect(m_addDirAction, SIGNAL(triggered(bool)),
            this, SLOT(slotAddDir()));

    m_deleteFilesAction = actionCollection()->addAction("delete");
    m_deleteFilesAction->setIcon(KIcon("archive-remove"));
    m_deleteFilesAction->setText(i18n("De&lete"));
    m_deleteFilesAction->setShortcut(Qt::Key_Delete);
    m_deleteFilesAction->setStatusTip(i18n("Click to delete the selected files"));
    connect(m_deleteFilesAction, SIGNAL(triggered(bool)),
            this, SLOT(slotDeleteFiles()));

    updateActions();
}

void Part::updateActions()
{
    bool isWritable = m_model->archive() && (!m_model->archive()->isReadOnly());

    m_previewAction->setEnabled(!isBusy() && (m_view->selectionModel()->selectedRows().count() == 1)
                                && isPreviewable(m_view->selectionModel()->currentIndex()));
    m_extractFilesAction->setEnabled(!isBusy() && (m_model->rowCount() > 0));
    m_addFilesAction->setEnabled(!isBusy() && isWritable);
    m_addDirAction->setEnabled(!isBusy() && isWritable);
    m_deleteFilesAction->setEnabled(!isBusy() && (m_view->selectionModel()->selectedRows().count() > 0)
                                    && isWritable);

    QMenu *m = m_extractFilesAction->menu();
    if (!m) {
        m = new QMenu(i18n("Recent folders"));
        m_extractFilesAction->setMenu(m);
        connect(m, SIGNAL(triggered(QAction*)),
                this, SLOT(slotQuickExtractFiles(QAction*)));
        QAction *header = m->addAction(i18n("Quick Extract To..."));
        header->setEnabled(false);
        header->setIcon(KIcon("archive-extract"));
    }

    while (m->actions().size() > 1) {
        m->removeAction(m->actions().last());
    }

    KConfigGroup conf(KGlobal::config(), "DirSelect Dialog");
    const QStringList dirHistory = conf.readPathEntry("History Items", QStringList());

    for (int i = 0; i < qMin(10, dirHistory.size()); ++i) {
        QAction *newAction = m->addAction(KUrl(dirHistory.at(i)).pathOrUrl());
        newAction->setData(KUrl(dirHistory.at(i)).pathOrUrl());
    }
}

void Part::slotQuickExtractFiles(QAction *triggeredAction)
{
    kDebug() << "Extract to " << triggeredAction->data().toString();

    const QString userDestination = triggeredAction->data().toString();
    QString finalDestinationDirectory;
    const QString detectedSubfolder = detectSubfolder();

    if (!isSingleFolderArchive()) {
        finalDestinationDirectory = userDestination +
                                    QDir::separator() + detectedSubfolder;
        QDir(userDestination).mkdir(detectedSubfolder);
    } else finalDestinationDirectory = userDestination;

    Kerfuffle::ExtractionOptions options;
    options["PreservePaths"] = true;
    QList<QVariant> files = selectedFiles();
    ExtractJob *job = m_model->extractFiles(files, finalDestinationDirectory, options);
    registerJob(job);

    connect(job, SIGNAL(result(KJob*)),
            this, SLOT(slotExtractionDone(KJob *)));

    job->start();

}

bool Part::isPreviewable(const QModelIndex& index) const
{
    return index.isValid() && (!m_model->entryForIndex(index)[ IsDirectory ].toBool());
}

void Part::selectionChanged()
{
    m_infoPanel->setIndexes(m_view->selectionModel()->selectedRows());
}

KAboutData* Part::createAboutData()
{
    return new KAboutData("ark", 0, ki18n("ArkPart"), "3.0");
}

bool Part::openFile()
{
    const QString localFile(localFilePath());

    if (arguments().metaData()["createNewArchive"] == "true") {
        if (QFileInfo(localFile).exists()) {
            int overwrite =  KMessageBox::questionYesNo(NULL, i18n("The file '%1' already exists. Would you like to open it instead?", localFile), i18nc("@title:window", "File Exists") , KGuiItem(i18n("Open File")), KGuiItem(i18n("Cancel")));

            if (overwrite == KMessageBox::No)
                return false;
        }
    } else {
        if (!QFileInfo(localFile).exists()) {
            KMessageBox::sorry(NULL, i18n("Error opening archive: the file '%1' was not found.", localFile), i18nc("@title:window", "Error Opening Archive"));
            return false;
        }
    }

    Kerfuffle::Archive *archive = Kerfuffle::factory(localFile);

    // TODO Post 4.3 string freeze:
    //      the isReadOnly check must be separate; see addtoarchive.cpp
    if (!archive || (arguments().metaData()["createNewArchive"] == "true" && archive->isReadOnly())) {
        QStringList mimeTypeList;
        QHash<QString, QString> mimeTypes;

        if (arguments().metaData()["createNewArchive"] == "true")
            mimeTypeList = Kerfuffle::supportedWriteMimeTypes();
        else
            mimeTypeList = Kerfuffle::supportedMimeTypes();

        foreach(const QString& mime, mimeTypeList) {
            KMimeType::Ptr mimePtr(KMimeType::mimeType(mime));
            if (mimePtr) {
                // Key = "application/zip", Value = "Zip Archive"
                mimeTypes[mime] = mimePtr->comment();
            }
        }

        QStringList mimeComments(mimeTypes.values());
        mimeComments.sort();

        bool ok;
        QString item(KInputDialog::getItem(i18nc("@title:window", "Unable to Determine Archive Type"),
                                           i18n("Ark was unable to determine the archive type of the filename.\n\nPlease choose the correct archive type below."),
                                           mimeComments,
                                           0,
                                           false,
                                           &ok));

        if (!ok || item.isEmpty())
            return false;

        archive = Kerfuffle::factory(localFile, mimeTypes.key(item));
    }

    if (!archive) {
        KMessageBox::sorry(NULL, i18n("Ark was not able to open the archive '%1'. No library capable of handling the file was found.", localFile), i18nc("@title:window", "Error Opening Archive"));
        return false;
    }

    KJob *job = m_model->setArchive(archive);
    registerJob(job);
    job->start();
    m_infoPanel->setIndex(QModelIndex());

    if (archive != 0 && arguments().metaData()["showExtractDialog"] == "true") {
        QTimer::singleShot(0, this, SLOT(slotExtractFiles()));
    }

    return (archive != 0);
}

bool Part::saveFile()
{
    return true;
}

bool Part::isBusy() const
{
    return m_busy;
}

void Part::slotLoadingStarted()
{
}

void Part::slotLoadingFinished(KJob *job)
{
    kDebug();

    if (job->error())
        if (arguments().metaData()["createNewArchive"] != "true")
            KMessageBox::sorry(NULL, i18n("Reading the archive '%1' failed with the error '%2'", localFilePath(), job->errorText()), i18nc("@title:window", "Error Opening Archive"));

    m_view->sortByColumn(0, Qt::AscendingOrder);
    m_view->expandToDepth(0);

    // After loading all files, resize the columns to fit all fields
    m_view->header()->resizeSections(QHeaderView::ResizeToContents);

    updateActions();
}

void Part::setReadyGui()
{
    kDebug();
    QApplication::restoreOverrideCursor();
    m_busy = false;
    m_view->setEnabled(true);
    updateActions();
}

void Part::setBusyGui()
{
    kDebug();
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    m_busy = true;
    m_view->setEnabled(false);
    updateActions();
}

void Part::setFileNameFromArchive()
{
    const QString prettyName = url().fileName();

    m_infoPanel->setPrettyFileName(prettyName);
    m_infoPanel->updateWithDefaults();

    emit setWindowCaption(prettyName);
}

void Part::slotPreview()
{
    slotPreview(m_view->selectionModel()->currentIndex());
}

void Part::slotPreview(const QModelIndex & index)
{
    if (!m_previewDir) {
        m_previewDir = new KTempDir();
    }

    if (!isPreviewable(index)) {
        return;
    }

    const ArchiveEntry& entry =  m_model->entryForIndex(index);

    if (!entry.isEmpty()) {
        Kerfuffle::ExtractionOptions options;
        options["PreservePaths"] = true;

        ExtractJob *job = m_model->extractFile(entry[ InternalID ], m_previewDir->name(), options);
        registerJob(job);
        connect(job, SIGNAL(result(KJob*)),
                this, SLOT(slotPreviewExtracted(KJob*)));
        job->start();
    }
}

void Part::slotPreviewExtracted(KJob *job)
{
    // FIXME: the error checking here isn't really working
    //        if there's an error or an overwrite dialog,
    //        the preview dialog will be launched anyway
    if (!job->error()) {
        const ArchiveEntry& entry =
            m_model->entryForIndex(m_view->selectionModel()->currentIndex());
        const QString fullName =
            m_previewDir->name() + '/' + entry[ FileName ].toString();
        ArkViewer::view(fullName, widget());
    } else {
        KMessageBox::error(widget(), job->errorString());
    }
    setReadyGui();
}

void Part::slotError(const QString& errorMessage, const QString& details)
{
    if (details.isEmpty()) {
        KMessageBox::error(widget(), errorMessage);
    } else {
        KMessageBox::detailedError(widget(), errorMessage, details);
    }
}

bool Part::isSingleFolderArchive() const
{
    return m_model->archive()->isSingleFolderArchive();
}

QString Part::detectSubfolder() const
{
    if (!m_model) {
        return QString();
    }

    return m_model->archive()->subfolderName();
}

void Part::slotExtractFiles()
{
    if (!m_model) {
        return;
    }

    QPointer<Kerfuffle::ExtractionDialog> dialog = new Kerfuffle::ExtractionDialog();

    if (m_view->selectionModel()->selectedRows().count() > 0) {
        dialog->setShowSelectedFiles(true);
    }

    dialog->setSingleFolderArchive(isSingleFolderArchive());
    dialog->setSubfolder(detectSubfolder());

    dialog->setCurrentUrl(QFileInfo(m_model->archive()->fileName()).path());

    if (dialog->exec()) {
        //this is done to update the quick extract menu
        updateActions();

        m_destinationDirectory = dialog->destinationDirectory().pathOrUrl();

        QVariantList files;

        //if the user has chosen to extract only selected entries, fetch these
        //from the listview
        if (!dialog->extractAllFiles()) {
            files = selectedFilesWithChildren();
        }

        kDebug() << "Selected " << files;

        Kerfuffle::ExtractionOptions options;

        if (dialog->preservePaths())
            options["PreservePaths"] = true;

        ExtractJob *job = m_model->extractFiles(files, m_destinationDirectory, options);
        registerJob(job);

        connect(job, SIGNAL(result(KJob*)),
                this, SLOT(slotExtractionDone(KJob *)));

        job->start();
    }

    delete dialog;
}

QList<QVariant> Part::selectedFilesWithChildren()
{
    Q_ASSERT(m_model);

    QModelIndexList toIterate = m_view->selectionModel()->selectedRows();

    for (int i = 0; i < toIterate.size(); ++i) {

        QModelIndex index = toIterate.at(i);

        for (int j = 0; j < m_model->rowCount(index); ++j) {
            QModelIndex child = m_model->index(j, 0, index);
            if (!toIterate.contains(child))
                toIterate << child;
        }

    }

    QVariantList ret;
    foreach(const QModelIndex & index, toIterate) {
        const ArchiveEntry& entry = m_model->entryForIndex(index);
        if (entry.contains(InternalID))
            ret << entry[ InternalID ];
    }
    return ret;
}

QList<QVariant> Part::selectedFiles()
{
    QStringList toSort;

    foreach(const QModelIndex & index, m_view->selectionModel()->selectedRows()) {
        const ArchiveEntry& entry = m_model->entryForIndex(index);
        toSort << entry[ InternalID ].toString();
    }

    toSort.sort();
    QVariantList ret;
    foreach(const QString &i, toSort) {
        ret << i;
    }
    return ret;
}

void Part::slotExtractionDone(KJob* job)
{
    kDebug();
    if (job->error()) {
        KMessageBox::error(widget(), job->errorString());
    } else {
        if (ArkSettings::openDestinationFolderAfterExtraction()) {
            KUrl destinationFolder(m_destinationDirectory);
            destinationFolder.cleanPath();

            KRun::runUrl(destinationFolder, "inode/directory", widget());
        }

        if (ArkSettings::closeAfterExtraction()) {
           emit quit();
        }
    }
}

void Part::adjustColumns()
{
    kDebug();

    m_view->header()->setResizeMode(0, QHeaderView::ResizeToContents);
}

void Part::slotAddFiles(const QStringList& filesToAdd, const QString& path)
{
    if (filesToAdd.isEmpty()) {
        return;
    }

    kDebug() << "Adding " << filesToAdd << " to " << path;
    kDebug() << "Warning, for now the path argument is not implemented";

    QStringList cleanFilesToAdd(filesToAdd);
    for (int i = 0; i < cleanFilesToAdd.size(); ++i) {
        QString& file = cleanFilesToAdd[i];
        if (QFileInfo(file).isDir()) {
            if (!file.endsWith('/')) file += '/';
        }
    }

    CompressionOptions options;

    QString firstPath = cleanFilesToAdd.first();
    if (firstPath.right(1) == "/") firstPath.chop(1);
    firstPath = QFileInfo(firstPath).dir().absolutePath();

    kDebug() << "Detected relative path to be " << firstPath;
    options["GlobalWorkDir"] = firstPath;

    AddJob *job = m_model->addFiles(cleanFilesToAdd, options);
    if (!job) return;

    connect(job, SIGNAL(result(KJob*)),
            this, SLOT(slotAddFilesDone(KJob*)));
    registerJob(job);
    job->start();
}

void Part::slotAddFiles()
{
    kDebug() ;
    const QStringList filesToAdd = KFileDialog::getOpenFileNames(KUrl("kfiledialog:///ArkAddFiles"), QString(), widget(), i18nc("@title:window", "Add Files"));

    slotAddFiles(filesToAdd);
}

void Part::slotAddDir()
{
    kDebug() ;
    const QString dirToAdd = KFileDialog::getExistingDirectory(KUrl("kfiledialog:///ArkAddFiles"), widget(), i18nc("@title:window", "Add Folder"));

    if (!dirToAdd.isEmpty()) {
        slotAddFiles(QStringList() << dirToAdd);
    }
}

void Part::slotAddFilesDone(KJob* job)
{
    kDebug() ;
    if (job->error()) {
        KMessageBox::error(widget(), job->errorString());
    }
}

void Part::slotDeleteFilesDone(KJob* job)
{
    kDebug() ;
    if (job->error()) {
        KMessageBox::error(widget(), job->errorString());
    }
}

void Part::slotDeleteFiles()
{
    kDebug() ;

    const int reallyDelete = KMessageBox::questionYesNo(NULL, i18n("Deleting these files is not undoable. Are you sure you want to do this?"), i18nc("@title:window", "Delete files") , KGuiItem(i18n("Delete files")), KGuiItem(i18n("Cancel")));

    if (reallyDelete == KMessageBox::No)
        return;

    DeleteJob *job = m_model->deleteFiles(selectedFilesWithChildren());
    connect(job, SIGNAL(result(KJob*)),
            this, SLOT(slotDeleteFilesDone(KJob*)));
    registerJob(job);
    job->start();
}

void Part::slotToggleInfoPanel(bool visible)
{
    QList<int> splitterSizes;

    if (visible) {
        splitterSizes = ArkSettings::splitterSizesWithBothWidgets();
    } else {
        splitterSizes = m_splitter->sizes();
        ArkSettings::setSplitterSizesWithBothWidgets(splitterSizes);
        splitterSizes[1] = 0;
    }

    m_splitter->setSizes(splitterSizes);
    slotUpdateSplitterSizes();
}

void Part::slotUpdateSplitterSizes()
{
    ArkSettings::setSplitterSizes(m_splitter->sizes());
    ArkSettings::self()->writeConfig();
}

void Part::slotSaveAs()
{
    KUrl saveUrl = KFileDialog::getSaveUrl(KUrl("kfiledialog:///ArkSaveAs/" + url().fileName()), QString(), widget());

    if ((saveUrl.isValid()) && (!saveUrl.isEmpty())) {
        if (KIO::NetAccess::exists(saveUrl, KIO::NetAccess::DestinationSide, widget())) {
            int overwrite = KMessageBox::warningContinueCancel(widget(),
                                                               i18n("A file named <b>%1</b> already exists. Are you sure you want to overwrite it?", saveUrl.fileName()),
                                                               QString(),
                                                               KGuiItem(i18n("Overwrite")));

            if (overwrite != KMessageBox::Continue) {
                return;
            }
        }

        KUrl srcUrl = KUrl::fromPath(localFilePath());

        if (!QFile::exists(localFilePath())) {
            if (url().isLocalFile()) {
                KMessageBox::error(widget(),
                                   i18n("The file <b>%1</b> cannot be copied to the specified location. The archive does not exist anymore.", localFilePath()));

                return;
            } else {
                srcUrl = url();
            }
        }

        KIO::Job *copyJob = KIO::file_copy(srcUrl, saveUrl, -1, KIO::Overwrite);

        if (!KIO::NetAccess::synchronousRun(copyJob, widget())) {
            KMessageBox::error(widget(),
                               i18n("The archive could not be saved as <b>%1</b>. Try to save it in another location.", saveUrl.pathOrUrl()));
        }
    }
}

} // namespace Ark
