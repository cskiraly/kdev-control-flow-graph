/***************************************************************************
 *   Copyright 2009 Sandro Andrade <sandro.andrade@gmail.com>              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/

#include "kdevcontrolflowgraphviewplugin.h"

#include <QAction>

#include <kgenericfactory.h>
#include <kaboutdata.h>
#include <kservice.h>
#include <kmessagebox.h>
#include <klocale.h>
#include <ktexteditor/view.h>
#include <ktexteditor/document.h>
#include <ktexteditor/cursor.h>

#include <interfaces/icore.h>
#include <interfaces/iuicontroller.h>
#include <interfaces/iprojectcontroller.h>
#include <interfaces/iproject.h>
#include <interfaces/ilanguagecontroller.h>
#include <interfaces/idocumentcontroller.h>
#include <interfaces/idocument.h>
#include <interfaces/context.h>
#include <interfaces/contextmenuextension.h>
#include <language/duchain/declaration.h>
#include <language/duchain/classdeclaration.h>
#include <language/duchain/classfunctiondeclaration.h>
#include <language/duchain/functiondefinition.h>
#include <language/duchain/types/functiontype.h>
#include <language/duchain/persistentsymboltable.h>
#include <language/interfaces/codecontext.h>
#include <language/backgroundparser/backgroundparser.h>
#include <language/backgroundparser/parsejob.h>
#include <language/duchain/codemodel.h>
#include <project/projectmodel.h>

#include "controlflowgraphview.h"
#include "duchaincontrolflow.h"
#include "dotcontrolflowgraph.h"

using namespace KDevelop;

K_PLUGIN_FACTORY(ControlFlowGraphViewFactory, registerPlugin<KDevControlFlowGraphViewPlugin>();)
K_EXPORT_PLUGIN(ControlFlowGraphViewFactory(KAboutData("kdevcontrolflowgraphview","kdecontrolflowgraph", ki18n("Control Flow Graph"), "0.1", ki18n("Control flow graph support in KDevelop"), KAboutData::License_GPL)))

class KDevControlFlowGraphViewFactory: public KDevelop::IToolViewFactory{
public:
    KDevControlFlowGraphViewFactory(KDevControlFlowGraphViewPlugin *plugin) : m_plugin(plugin) {}
    virtual QWidget* create(QWidget *parent = 0)
    {
        return new ControlFlowGraphView(m_plugin, parent);
    }
    virtual Qt::DockWidgetArea defaultPosition()
    {
        return Qt::BottomDockWidgetArea;
    }
    virtual QString id() const
    {
        return "org.kdevelop.ControlFlowGraphView";
    }
private:
    KDevControlFlowGraphViewPlugin *m_plugin;
};

KDevControlFlowGraphViewPlugin::KDevControlFlowGraphViewPlugin (QObject *parent, const QVariantList &)
:
KDevelop::IPlugin (ControlFlowGraphViewFactory::componentData(), parent),
m_toolViewFactory(new KDevControlFlowGraphViewFactory(this)),
m_activeToolView(0)
{
    core()->uiController()->addToolView(i18n("Control Flow Graph"), m_toolViewFactory);

    QObject::connect(core()->documentController(), SIGNAL(textDocumentCreated(KDevelop::IDocument *)),
		     this, SLOT(textDocumentCreated(KDevelop::IDocument *)));
    QObject::connect(core()->projectController(), SIGNAL(projectOpened(KDevelop::IProject*)),
		     this, SLOT(projectOpened(KDevelop::IProject*)));
    QObject::connect(core()->projectController(), SIGNAL(projectClosed(KDevelop::IProject*)),
		     this, SLOT(projectClosed(KDevelop::IProject*)));
    QObject::connect(core()->languageController()->backgroundParser(), SIGNAL(parseJobFinished(KDevelop::ParseJob*)),
		     this, SLOT(parseJobFinished(KDevelop::ParseJob*)));
		     
    m_exportControlFlowGraph = new QAction(i18n("Export Control Flow Graph"), this);
    connect(m_exportControlFlowGraph, SIGNAL(triggered(bool)), this, SLOT(slotExportControlFlowGraph(bool)));
    m_exportClassControlFlowGraph = new QAction(i18n("Export Class Control Flow Graph"), this);
    connect(m_exportClassControlFlowGraph, SIGNAL(triggered(bool)), this, SLOT(slotExportClassControlFlowGraph(bool)));
    m_exportProjectControlFlowGraph = new QAction(i18n("Export Project Control Flow Graph"), this);
    connect(m_exportProjectControlFlowGraph, SIGNAL(triggered(bool)), this, SLOT(slotExportProjectControlFlowGraph(bool)));
}

KDevControlFlowGraphViewPlugin::~KDevControlFlowGraphViewPlugin()
{
}

void KDevControlFlowGraphViewPlugin::unload()
{
    core()->uiController()->removeToolView(m_toolViewFactory);
}

void KDevControlFlowGraphViewPlugin::registerToolView(ControlFlowGraphView *view)
{
    m_toolViews << view;
}

void KDevControlFlowGraphViewPlugin::unRegisterToolView(ControlFlowGraphView *view)
{
    m_toolViews.removeAll(view);
}

ControlFlowGraphFileDialog *KDevControlFlowGraphViewPlugin::exportControlFlowGraph(ControlFlowGraphFileDialog::OpeningMode mode)
{
    ControlFlowGraphFileDialog *fileDialog = new ControlFlowGraphFileDialog(KUrl(), "*.png|PNG (Portable Network Graphics)\n*.jpg *.jpeg|JPG \\/ JPEG (Joint Photographic Expert Group)\n*.gif|GIF (Graphics Interchange Format)\n*.svg *.svgz|SVG (Scalable Vector Graphics)\n*.dia|DIA (Dia Structured Diagrams)\n*.fig|FIG\n*.pdf|PDF (Portable Document Format)\n*.dot|DOT (Graph Description Language)", (QWidget *) core()->uiController()->activeMainWindow(), i18n("Export Control Flow Graph"), mode);
    fileDialog->exec();
    QString fileName = fileDialog->selectedFile();
    if (!fileName.isEmpty())
    {
	// Note: this is going to be removed with KDE 4.4 since getSaveFileName will support a KFileDialog::ConfirmOverwrite option
	int code = KMessageBox::Yes;
	if (QFile(fileName).exists())
	    code = KMessageBox::warningYesNo((QWidget *) core()->uiController()->activeMainWindow(),
						   i18n("File already exists. Are you sure you want to overwrite it ?"),
						   i18n("Export Control Flow Graph"));

	if (code == KMessageBox::Yes)
	    return fileDialog;
    }
    return 0;
}

KDevelop::ContextMenuExtension
KDevControlFlowGraphViewPlugin::contextMenuExtension(KDevelop::Context* context)
{
    KDevelop::ContextMenuExtension extension;

    if (context->hasType(Context::CodeContext) || context->hasType(Context::EditorContext))
    {
	KDevelop::DeclarationContext *codeContext = dynamic_cast<KDevelop::DeclarationContext*>(context);

	if (!codeContext)
	    return extension;

	DUChainReadLocker readLock(DUChain::lock());
	Declaration *declaration(codeContext->declaration().data());

	// Insert action for generating control flow graph from method
	if (declaration && declaration->type<KDevelop::FunctionType>() && (declaration->isDefinition() || FunctionDefinition::definition(declaration)))
	{
	    m_exportControlFlowGraph->setData(QVariant::fromValue(DUChainBasePointer(declaration)));
	    extension.addAction(KDevelop::ContextMenuExtension::ExtensionGroup, m_exportControlFlowGraph);
	}
	// Insert action for generating control flow graph for the whole class
	else if (declaration && declaration->kind() == Declaration::Type &&
		declaration->internalContext() &&
		declaration->internalContext()->type() == DUContext::Class)
	{
	    m_exportClassControlFlowGraph->setData(QVariant::fromValue(DUChainBasePointer(declaration)));
	    extension.addAction(KDevelop::ContextMenuExtension::ExtensionGroup, m_exportClassControlFlowGraph);
	}
    }
    else if (context->hasType(Context::ProjectItemContext))
    {
	KDevelop::ProjectItemContext *projectItemContext = dynamic_cast<KDevelop::ProjectItemContext*>(context);
	if (projectItemContext)
	{
	    QList<ProjectBaseItem *> items = projectItemContext->items();
	    foreach(ProjectBaseItem *item, items)
	    {
		ProjectFolderItem *folder = item->folder();
		if (folder && folder->isProjectRoot())
		{
		    m_exportProjectControlFlowGraph->setData(QVariant::fromValue(folder->folderName()));
		    extension.addAction(KDevelop::ContextMenuExtension::ExtensionGroup, m_exportProjectControlFlowGraph);
		}
	    }
	}
    }
    return extension;
}

void KDevControlFlowGraphViewPlugin::projectOpened(KDevelop::IProject* project)
{
    Q_UNUSED(project);
    foreach (ControlFlowGraphView *controlFlowGraphView, m_toolViews)
	controlFlowGraphView->setProjectButtonsEnabled(true);
    refreshActiveToolView();
}

void KDevControlFlowGraphViewPlugin::projectClosed(KDevelop::IProject* project)
{
    Q_UNUSED(project);
    if (core()->projectController()->projectCount() == 0)
    {
	foreach (ControlFlowGraphView *controlFlowGraphView, m_toolViews)
	{
	    controlFlowGraphView->setProjectButtonsEnabled(false);
	    controlFlowGraphView->newGraph();
	}
    }
    refreshActiveToolView();
}

void KDevControlFlowGraphViewPlugin::parseJobFinished(KDevelop::ParseJob* parseJob)
{
    if (core()->documentController()->activeDocument() &&
	parseJob->document().toUrl() == core()->documentController()->activeDocument()->url())
	refreshActiveToolView();
}

void KDevControlFlowGraphViewPlugin::textDocumentCreated(KDevelop::IDocument *document)
{
    connect(document->textDocument(), SIGNAL(viewCreated(KTextEditor::Document *, KTextEditor::View *)),
	    this, SLOT(viewCreated(KTextEditor::Document *, KTextEditor::View *)));
}

void KDevControlFlowGraphViewPlugin::viewCreated(KTextEditor::Document *document, KTextEditor::View *view)
{
    Q_UNUSED(document);
    connect(view, SIGNAL(cursorPositionChanged(KTextEditor::View *, const KTextEditor::Cursor &)),
	    this, SLOT(cursorPositionChanged(KTextEditor::View *, const KTextEditor::Cursor &)));
    connect(view, SIGNAL(destroyed(QObject *)), this, SLOT(viewDestroyed(QObject *)));
    connect(view, SIGNAL(focusIn(KTextEditor::View *)), this, SLOT(focusIn(KTextEditor::View *)));
}

void KDevControlFlowGraphViewPlugin::viewDestroyed(QObject *object)
{
    Q_UNUSED(object);
    if (!core()->documentController()->activeDocument() && m_activeToolView)
	m_activeToolView->newGraph();
}

void KDevControlFlowGraphViewPlugin::focusIn(KTextEditor::View *view)
{
    if (view)
	cursorPositionChanged(view, view->cursorPosition());
}

void KDevControlFlowGraphViewPlugin::cursorPositionChanged(KTextEditor::View *view, const KTextEditor::Cursor &cursor)
{
    if (m_activeToolView)
	m_activeToolView->cursorPositionChanged(view, cursor);
}

void KDevControlFlowGraphViewPlugin::refreshActiveToolView()
{
    if (m_activeToolView)
	m_activeToolView->refreshGraph();
}

void KDevControlFlowGraphViewPlugin::slotExportControlFlowGraph(bool)
{
    Q_ASSERT(qobject_cast<QAction *>(sender()));
    QAction *action = static_cast<QAction *>(sender());
    Q_ASSERT(action->data().canConvert<DUChainBasePointer>());
    DeclarationPointer declarationPointer = qvariant_cast<DUChainBasePointer>(action->data()).dynamicCast<Declaration>();
    Declaration *declaration = declarationPointer.data();

    if (!declaration->isDefinition())
    {
	declaration = FunctionDefinition::definition(declaration);
	if (!declaration || !declaration->internalContext())
	{
	    KMessageBox::error((QWidget *) core()->uiController()->activeMainWindow(), i18n("Couldn't generate control flow graph"));
	    return;
	}
    }
    ControlFlowGraphFileDialog *fileDialog;
    if ((fileDialog = exportControlFlowGraph()))
    {
	DUChainControlFlow *duchainControlFlow = new DUChainControlFlow;
	DotControlFlowGraph *dotControlFlowGraph = new DotControlFlowGraph;

	configureDuchainControlFlow(duchainControlFlow, dotControlFlowGraph, fileDialog);

	duchainControlFlow->generateControlFlowForDeclaration(declaration, declaration->topContext(), declaration->internalContext());
	dotControlFlowGraph->exportGraph(fileDialog->selectedFile());

	delete dotControlFlowGraph;
	delete duchainControlFlow;
    }
}

void KDevControlFlowGraphViewPlugin::slotExportClassControlFlowGraph(bool)
{
    Q_ASSERT(qobject_cast<QAction *>(sender()));
    QAction *action = static_cast<QAction *>(sender());
    Q_ASSERT(action->data().canConvert<DUChainBasePointer>());
    DeclarationPointer declarationPointer = qvariant_cast<DUChainBasePointer>(action->data()).dynamicCast<Declaration>();
    Declaration *declaration = declarationPointer.data();

    ControlFlowGraphFileDialog *fileDialog;
    if ((fileDialog = exportControlFlowGraph(ControlFlowGraphFileDialog::ForClassConfigurationButtons)))
    {
	DUChainControlFlow *duchainControlFlow = new DUChainControlFlow;
	DotControlFlowGraph *dotControlFlowGraph = new DotControlFlowGraph;

	configureDuchainControlFlow(duchainControlFlow, dotControlFlowGraph, fileDialog);

	DUChainReadLocker readLock(DUChain::lock());
	ClassFunctionDeclaration *functionDeclaration;
	if (declaration->internalContext())
	    foreach (Declaration *decl, declaration->internalContext()->localDeclarations())
		if ((functionDeclaration = dynamic_cast<ClassFunctionDeclaration *>(decl)))
		{
		    Declaration *functionDefinition = FunctionDefinition::definition(functionDeclaration);
		    if (functionDefinition)
			duchainControlFlow->generateControlFlowForDeclaration(functionDefinition, functionDefinition->topContext(), functionDefinition->internalContext());
		}

	dotControlFlowGraph->exportGraph(fileDialog->selectedFile());

	delete dotControlFlowGraph;
	delete duchainControlFlow;
    }
}

void KDevControlFlowGraphViewPlugin::slotExportProjectControlFlowGraph(bool)
{
    Q_ASSERT(qobject_cast<QAction *>(sender()));
    QAction *action = static_cast<QAction *>(sender());
    Q_ASSERT(action->data().canConvert<QString>());
    QString projectName = qvariant_cast<QString>(action->data());
    IProject *project = core()->projectController()->findProjectByName(projectName);

    ControlFlowGraphFileDialog *fileDialog;
    if ((fileDialog = exportControlFlowGraph(ControlFlowGraphFileDialog::ForClassConfigurationButtons)))
    {
	DUChainControlFlow *duchainControlFlow = new DUChainControlFlow;
	DotControlFlowGraph *dotControlFlowGraph = new DotControlFlowGraph;

	configureDuchainControlFlow(duchainControlFlow, dotControlFlowGraph, fileDialog);

	DUChainReadLocker readLock(DUChain::lock());
	foreach(const IndexedString &file, project->fileSet())
	{
	    uint codeModelItemCount = 0;
	    const CodeModelItem *codeModelItems = 0;
	    CodeModel::self().items(file, codeModelItemCount, codeModelItems);
	    
	    for (uint codeModelItemIndex = 0; codeModelItemIndex < codeModelItemCount; ++codeModelItemIndex)
	    {
		const CodeModelItem &item = codeModelItems[codeModelItemIndex];
		
		if ((item.kind & CodeModelItem::Class) && !item.id.identifier().last().toString().isEmpty())
		{
		    uint declarationCount = 0;
		    const IndexedDeclaration *declarations = 0;
		    PersistentSymbolTable::self().declarations(item.id.identifier(), declarationCount, declarations);
		    for (uint i = 0; i < declarationCount; ++i)
		    {
			Declaration *declaration = dynamic_cast<Declaration *>(declarations[i].declaration());
			if (declaration && !declaration->isForwardDeclaration() && declaration->internalContext())
			{
			    ClassFunctionDeclaration *functionDeclaration;
			    foreach (Declaration *decl, declaration->internalContext()->localDeclarations())
				if ((functionDeclaration = dynamic_cast<ClassFunctionDeclaration *>(decl)))
				{
				    Declaration *functionDefinition = FunctionDefinition::definition(functionDeclaration);
				    if (functionDefinition)
					duchainControlFlow->generateControlFlowForDeclaration(functionDefinition, functionDefinition->topContext(), functionDefinition->internalContext());
				}
			    break;
			}
		    }
		}
	    }
	}
	
	dotControlFlowGraph->exportGraph(fileDialog->selectedFile());

	delete dotControlFlowGraph;
	delete duchainControlFlow;
    }
}

void KDevControlFlowGraphViewPlugin::setActiveToolView(ControlFlowGraphView *activeToolView)
{
    m_activeToolView = activeToolView;
    refreshActiveToolView();
}

void KDevControlFlowGraphViewPlugin::configureDuchainControlFlow(DUChainControlFlow *duchainControlFlow, DotControlFlowGraph *dotControlFlowGraph, ControlFlowGraphFileDialog *fileDialog)
{
    duchainControlFlow->setControlFlowMode(fileDialog->controlFlowMode());
    duchainControlFlow->setClusteringModes(fileDialog->clusteringModes());
    duchainControlFlow->setMaxLevel(fileDialog->maxLevel());
    duchainControlFlow->setUseFolderName(fileDialog->useFolderName());
    duchainControlFlow->setUseShortNames(fileDialog->useShortNames());
    duchainControlFlow->setDrawIncomingArcs(fileDialog->drawIncomingArcs());

    connect(duchainControlFlow,  SIGNAL(foundRootNode(const QStringList &, const QString &)),
	    dotControlFlowGraph, SLOT  (foundRootNode(const QStringList &, const QString &)));
    connect(duchainControlFlow,  SIGNAL(foundFunctionCall(const QStringList &, const QString &, const QStringList &, const QString &)),
	    dotControlFlowGraph, SLOT  (foundFunctionCall(const QStringList &, const QString &, const QStringList &, const QString &)));
    connect(duchainControlFlow,  SIGNAL(clearGraph()), dotControlFlowGraph, SLOT(clearGraph()));

    dotControlFlowGraph->prepareNewGraph();
}

#include "kdevcontrolflowgraphviewplugin.moc"
