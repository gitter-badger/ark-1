/*
 * ark -- archiver for the KDE project
 *
 * Copyright (C) 2009 Harald Hvaal
 * <haraldhv atatatat stud.ntnu.no>
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

#include "cliinterface.h"

#include <QFile>
#include <QDir>
#include <QDateTime>
#include <KProcess>
#include <KStandardDirs>
#include <KDebug>
#include <KLocale>
#include <QEventLoop>
#include <QThread>
#include <KProcess>
#include <kptyprocess.h>
#include <kptydevice.h>

namespace Kerfuffle
{

	CliInterface::CliInterface( const QString& filename, QObject *parent)
		: ReadWriteArchiveInterface(filename, parent),
		m_process(NULL),
		m_loop(NULL)
	{


	}

	CliInterface::~CliInterface()
	{

	}

	bool CliInterface::list()
	{
		ParameterList param = parameterList();

		Q_ASSERT(param.contains(ListProgram));

		bool ret = findProgramInPath(param.value(ListProgram).toString());
		if (!ret) {
			error("TODO could not find program");
			return false;
		}

		ret = createProcess();
		if (!ret) {
			error("TODO could not find program");
			return false;
		}

		QStringList args = param.value(ListArgs).toStringList();
		substituteListVariables(args);

		executeProcess(m_program, args);

		return true;
	}

	bool CliInterface::copyFiles( const QList<QVariant> & files, const QString & destinationDirectory, ExtractionOptions options )
	{
		ParameterList param = parameterList();

		Q_ASSERT(param.contains(ExtractProgram));

		bool ret = findProgramInPath(param.value(ExtractProgram).toString());
		if (!ret) {
			error("TODO could not find program");
			return false;
		}

		ret = createProcess();
		if (!ret) {
			error("TODO could not find program");
			return false;
		}

		QStringList args = param.value(ExtractArgs).toStringList();
		substituteCopyVariables(args, files, destinationDirectory, options);

		QString globalWorkdir = options.value("GlobalWorkDir").toString();
		if (!globalWorkdir.isEmpty()) {
			kDebug( 1601 ) << "GlobalWorkDir is set, changing dir to " << globalWorkdir;
			QDir::setCurrent(globalWorkdir);
		}

		executeProcess(m_program, args);

		return true;
	}


	bool CliInterface::addFiles( const QStringList & files, const CompressionOptions& options )
	{

		return false;
	}

	bool CliInterface::deleteFiles( const QList<QVariant> & files )
	{

		return false;
	}

	bool CliInterface::createProcess()
	{
		if (m_process)
			return false;

		m_process = new KPtyProcess;
		m_process->setOutputChannelMode( KProcess::SeparateChannels );

		connect( m_process, SIGNAL( started() ), SLOT( started() ) );
		connect( m_process, SIGNAL( readyReadStandardOutput() ), SLOT( readStdout() ) );
		connect( m_process, SIGNAL( readyReadStandardError() ), SLOT( readFromStderr() ) );
		connect( m_process, SIGNAL( finished( int, QProcess::ExitStatus ) ), SLOT( finished( int, QProcess::ExitStatus ) ) );

		if (QMetaType::type("QProcess::ExitStatus") == 0)
			qRegisterMetaType<QProcess::ExitStatus>("QProcess::ExitStatus");
		return true;
	}

	bool CliInterface::executeProcess(const QString& path, const QStringList & args)
	{
		Q_ASSERT(!path.isEmpty());

		kDebug( 1601 ) << "Executing " << path << args;

		m_process->setProgram( path, args );
		m_process->setNextOpenMode( QIODevice::ReadWrite | QIODevice::Unbuffered );
		m_process->start();
		QEventLoop loop;
		m_loop = &loop;
		bool ret = loop.exec( QEventLoop::WaitForMoreEvents );
		m_loop = 0;

		delete m_process;
		m_process = NULL;
		/*
		   if (!m_errorMessages.isEmpty())
		   {
		   error(m_errorMessages.join("\n"));
		   return false;
		   }
		   else if (ret && !m_userCancelled) {
		   error(i18n("Unknown error when extracting files"));
		   return false;
		   }
		   else
		   {
		   return true;
		   }
		   */

		return true;
	}


	void CliInterface::started()
	{
		//m_state = 0;
		m_errorMessages.clear();
		m_userCancelled = false;
	}

	void CliInterface::finished( int exitCode, QProcess::ExitStatus exitStatus)
	{
		if ( !m_process )
			return;

		progress(1.0);

		if ( m_loop )
		{
			m_loop->exit( exitStatus == QProcess::CrashExit ? 1 : 0 );
		}
	}

	void CliInterface::readFromStderr()
	{
		if ( !m_process )
			return;

		QByteArray stdErrData = m_process->readAllStandardError();

		kDebug( 1601 ) << "ERROR" << stdErrData.size() << stdErrData;

		if ( !stdErrData.isEmpty() )
		{
			//if (handlePasswordPrompt(stdErrData))
				//return;
			//else if (handleOverwritePrompt(stdErrData))
			//	return;
			//else
			{
				m_errorMessages << QString::fromLocal8Bit(stdErrData);
			}
		}
	}

	void CliInterface::readStdout()
	{
		if ( !m_process )
			return;

		m_stdOutData += m_process->readAllStandardOutput();

		// process all lines until the last '\n' or backspace
		int indx = m_stdOutData.lastIndexOf('\010');
		if (indx == -1) indx = m_stdOutData.lastIndexOf('\n');
		if (indx == -1) return;

		QString leftString = QString::fromLocal8Bit(m_stdOutData.left(indx + 1));
		const QStringList lines = leftString.split( QRegExp("[\\n\\010]"), QString::SkipEmptyParts );
		foreach(const QString &line, lines) {

			kDebug( 1601 ) << line;

		}

		m_stdOutData.remove(0, indx + 1);
	}


	bool CliInterface::findProgramInPath(const QString& program)
	{
		m_program = KStandardDirs::findExe( program );
		return !m_program.isEmpty();
	}

	void CliInterface::substituteCopyVariables(QStringList& params, const QList<QVariant> & files, const QString & destinationDirectory, ExtractionOptions options)
	{
		for (int i = 0; i < params.size(); ++i) {
			QString parameter = params.at(i);

			if (parameter == "$Archive") {
				params[i] = filename();
			}

			if (parameter == "$Files") {
				params.removeAt(i);
				for (int j = 0; j < files.count(); ++j)
					params.insert(i + j, files.at(j).toString());

			}
		}
	}

	void CliInterface::substituteListVariables(QStringList& params)
	{
		for (int i = 0; i < params.size(); ++i) {
			QString parameter = params.at(i);

			if (parameter == "$Archive") {
				params[i] = filename();
			}

		}
	}

}

#include "cliinterface.moc"
