/*                                                                               ark -- archiver for the KDE project
 
 Copyright (C)
 
 1997-1999: Rob Palmbos palm9744@kettering.edu
 1999: Francois-Xavier Duranceau duranceau@kde.org
 1999-2000: Corel Corporation (author: Emily Ezust emilye@corel.com)
 
 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 
*/
#include <kdebug.h>
#include <kurl.h>
#include <kfiledialog.h>
#include <qvbox.h>
#include <qmessagebox.h>
#include <qcheckbox.h>
#include <qfileinfo.h>
#include <klocale.h>
#include <qbuttongroup.h>
#include <qpushbutton.h>
#include <qradiobutton.h>
#include <qlineedit.h>
#include "arksettings.h"
#include "extractdlg.h"

ExtractDlg::ExtractDlg(ArchType _archtype, ArkSettings *_settings)
  : QTabDialog(0, "extractdialog", true), m_settings(_settings),
    m_archtype(_archtype)
{
  setCaption(i18n("ark - Extract"));

  setupFirstTab();
  setupSecondTab();

  setOKButton();
  setCancelButton();

  connect(m_patternLE, SIGNAL(textChanged(const QString &)),
	  this, SLOT(choosePattern()));

  connect(m_extractDirLE, SIGNAL(returnPressed()), this, SLOT(accept()));
  connect(m_patternLE, SIGNAL(returnPressed()), this, SLOT(accept()));
}

void ExtractDlg::setupFirstTab()
{
  QVBox *firstpage = new QVBox( this );
  firstpage->setMargin( 5 );

  QLabel *extractToLabel = new QLabel(firstpage);
  extractToLabel->setText(i18n("Extract to: "));
  m_extractDirLE = new QLineEdit(firstpage);
  m_extractDirLE->setText(m_settings->getExtractDir());

  QPushButton *browseButton = new QPushButton(firstpage);
  browseButton->setText(i18n("Browse..."));

  QButtonGroup *bg = new QButtonGroup( 1, QGroupBox::Horizontal,
				       i18n("Files to Extract"), firstpage );
  m_radioCurrent = new QRadioButton("Current", bg);
  m_radioCurrent->setText(i18n("Current"));
  m_radioAll = new QRadioButton("All", bg);
  m_radioAll->setText(i18n("All"));
  m_radioSelected = new QRadioButton("Selected Files", bg);
  m_radioSelected->setText(i18n("Selected Files"));
  m_radioSelected->setChecked(true);
  m_radioPattern = new QRadioButton("By Pattern", bg);
  m_radioPattern->setText(i18n("Pattern"));

  QLabel *patternLabel = new QLabel(firstpage, "label");
  patternLabel->setText(i18n("Pattern:"));

  m_patternLE = new QLineEdit(firstpage, "le");

  addTab(firstpage, i18n("Extract"));

  QObject::connect(browseButton, SIGNAL(clicked()),
		   this, SLOT(browse()));
}

void ExtractDlg::setupSecondTab()
{
  if (m_archtype == LHA_FORMAT || m_archtype == AA_FORMAT)
    return; // there are no extract options for LHA or AA

  QVBox *secondpage = new QVBox( this );
  secondpage->setMargin( 5 );

  QButtonGroup *bg = new QButtonGroup( 1, QGroupBox::Horizontal,
				       0, secondpage );


  // use m_archtype to determine what goes here... 
  // these are the advanced options

  switch(m_archtype)
    {
    case ZIP_FORMAT:
      bg->setTitle(i18n("ZIP Options"));

      m_cbOverwrite = new QCheckBox(i18n("Overwrite files"), bg);
      if (m_settings->getZipExtractOverwrite())
	m_cbOverwrite->setChecked(true);

      m_cbToLower = new QCheckBox(i18n("Convert filenames to lowercase"),
				  bg);
      if (m_settings->getZipExtractLowerCase())
	m_cbToLower->setChecked(true);
      break;
    case TAR_FORMAT:
      bg->setTitle(i18n("TAR Options"));

      m_cbOverwrite = new QCheckBox(i18n("Overwrite files"), bg);
      if (m_settings->getTarOverwriteFiles())
	m_cbOverwrite->setChecked(true);

      m_cbPreservePerms = new QCheckBox(i18n("Preserve permissions"), bg);
      if (m_settings->getTarPreservePerms())
	m_cbPreservePerms->setChecked(true);

      break;
    case RAR_FORMAT:
      bg->setTitle(i18n("RAR Options"));

      m_cbOverwrite = new QCheckBox(i18n("Overwrite files"), bg);
      if (m_settings->getRarOverwriteFiles())
	m_cbOverwrite->setChecked(true);

      m_cbToLower = new QCheckBox(i18n("Convert filenames to lowercase"),
				  bg);
      if (m_settings->getRarExtractLowerCase())
	m_cbToLower->setChecked(true);

      m_cbToUpper = new QCheckBox(i18n("Convert filenames to uppercase"),
				  bg);
      if (m_settings->getRarExtractUpperCase())
	m_cbToUpper->setChecked(true);
      break;
    case ZOO_FORMAT:
      bg->setTitle(i18n("ZOO Options"));
      m_cbOverwrite = new QCheckBox(i18n("Overwrite files"), bg);
      if (m_settings->getZooOverwriteFiles())
	m_cbOverwrite->setChecked(true);
      break;
    default:
      // shouldn't ever get here!
      ASSERT(0);
      break;
    }
  
  addTab(secondpage, i18n("Advanced"));
}


void ExtractDlg::accept()
{
  kDebugInfo( 1601, "+ExtractDlg::accept");
  if (! QFileInfo(m_extractDirLE->text()).isDir())
  {
    QMessageBox::warning(this, i18n("Error"),
			   i18n("Please provide a valid directory"));
    return;
  }

  // you need to change the settings to change the fixed dir.
  m_settings->setLastExtractDir(m_extractDirLE->text());

  // save settings

  switch(m_archtype)
    {
    case ZIP_FORMAT:
      m_settings->setZipExtractOverwrite(m_cbOverwrite->isChecked());
      m_settings->setZipExtractLowerCase(m_cbToLower->isChecked());
      break;
    case TAR_FORMAT:
      m_settings->setTarOverwriteFiles(m_cbOverwrite->isChecked());
      m_settings->setTarPreservePerms(m_cbPreservePerms->isChecked());
      break;
    case AA_FORMAT:
    case LHA_FORMAT:
      break; // do nothing
    case RAR_FORMAT:
      m_settings->setRarOverwriteFiles(m_cbOverwrite->isChecked());
      m_settings->setRarExtractLowerCase(m_cbToLower->isChecked());
      m_settings->setRarExtractUpperCase(m_cbToUpper->isChecked());
      break;
    case ZOO_FORMAT:
      m_settings->setZooOverwriteFiles(m_cbOverwrite->isChecked());
      break;
    default:
      // shouldn't ever get here!
      ASSERT(0);
      break;
    }

  if (m_radioPattern->isChecked())
  {
    if (strcmp(m_patternLE->text(), "") == 0)
    {
      // pattern selected but no pattern? Ask user to select a pattern.
      QMessageBox::warning(this, i18n("Error"),
			   i18n("Please provide a pattern"));
      return;
    }
    else
      {
	emit pattern(m_patternLE->text());
      }
  }

  // I made it! so nothing's wrong.
  QTabDialog::accept();
  kDebugInfo( 1601, "-ExtractDlg::accept");
}


void ExtractDlg::browse() // slot
{
  QString dirName
    = KFileDialog::getExistingDirectory(m_settings->getExtractDir(), 0,
					i18n("Select an Extract Directory"));
  if (! dirName.isEmpty())
  {
    m_extractDirLE->setText(dirName);
  }
}




int ExtractDlg::extractOp()
{
// which kind of extraction shall we do?

  if (m_radioCurrent->isChecked())
    return ExtractDlg::Current;
  if(m_radioAll->isChecked())
    return ExtractDlg::All;
  if(m_radioSelected->isChecked())
    return ExtractDlg::Selected;
  if(m_radioPattern->isChecked())
    return ExtractDlg::Pattern;
  return -1;
}



#include "extractdlg.moc"
