/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *   for detail see the LICENCE text file.                                 *
 *   Jürgen Riegel 2002                                                    *
 *   Copyright (c) 2014 Luke Parry <l.parry@warwick.ac.uk>                 *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"
#ifndef _PreComp_
# include <sstream>
# include <QCoreApplication>
# include <QDir>
# include <QFile>
# include <QFileInfo>
# include <QMessageBox>
# include <QRegExp>
#endif

#include <QStringBuilder>

#include <QGraphicsView>
#include <QPainter>
#include <QSvgRenderer>
#include <QSvgGenerator>

#include <vector>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/FeaturePython.h>
#include <App/PropertyGeo.h>
#include <Base/Console.h>
#include <Base/Parameter.h>
#include <Gui/Action.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Command.h>
#include <Gui/Control.h>
#include <Gui/Document.h>
#include <Gui/Selection.h>
#include <Gui/MainWindow.h>
#include <Gui/FileDialog.h>
#include <Gui/ViewProvider.h>
#include <Gui/WaitCursor.h>

#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/Part2DObject.h>
#include <Mod/Spreadsheet/App/Sheet.h>

#include <Mod/TechDraw/App/DrawPage.h>
#include <Mod/TechDraw/App/DrawViewPart.h>
#include <Mod/TechDraw/App/DrawProjGroupItem.h>
#include <Mod/TechDraw/App/DrawProjGroup.h>
#include <Mod/TechDraw/App/DrawViewDimension.h>
#include <Mod/TechDraw/App/DrawViewClip.h>
#include <Mod/TechDraw/App/DrawViewAnnotation.h>
#include <Mod/TechDraw/App/DrawViewSymbol.h>
#include <Mod/TechDraw/App/DrawViewDraft.h>
#include <Mod/TechDraw/Gui/QGVPage.h>

#include "MDIViewPage.h"
#include "TaskProjGroup.h"
#include "ViewProviderPage.h"

using namespace TechDrawGui;
using namespace std;


//===========================================================================
// utility routines
//===========================================================================

//! find a DrawPage in Selection or Document
//TODO: code is duplicated in CommandCreateDims and CommandDecorate
TechDraw::DrawPage* _findPage(Gui::Command* cmd)
{
    //check if a DrawPage is currently displayed
    auto mdiView( Gui::getMainWindow()->activeWindow() );
    auto mvp( dynamic_cast<MDIViewPage *>(mdiView) );
    if (mvp) {
        return mvp->getQGVPage()->getDrawPage();
    } else {
        TechDraw::DrawPage* page(nullptr);

        //DrawPage not displayed, check Selection and/or Document for a DrawPage
        auto drawPageType( TechDraw::DrawPage::getClassTypeId() );
        auto selPages( cmd->getSelection().getObjectsOfType(drawPageType) );
        if (selPages.empty()) {                                            //no page in selection
            selPages = cmd->getDocument()->getObjectsOfType(drawPageType);
            if (selPages.empty()) {                                        //no page in document
                QMessageBox::warning(Gui::getMainWindow(), QObject::tr("No page found"),
                                     QObject::tr("Create a page first."));
                return page;
            } else if (selPages.size() > 1) {                              //multiple pages in document
                QMessageBox::warning(Gui::getMainWindow(), QObject::tr("Too many pages"),
                                     QObject::tr("Can not determine correct page."));
                return page;
            } else {                                                       //use only page in document
                page = dynamic_cast<TechDraw::DrawPage*>(selPages.front());
            }
        } else if (selPages.size() > 1) {                                  //multiple pages in selection
            QMessageBox::warning(Gui::getMainWindow(), QObject::tr("Too many pages"),
                                 QObject::tr("Select exactly 1 page."));
            return page;
        } else {                                                           //use only page in selection
            page = dynamic_cast<TechDraw::DrawPage *>(selPages.front());
        }

        return page;
    }
}

bool isDrawingPageActive(Gui::Document *doc)
{
    if (doc)
        // checks if a DrawPage Viewprovider is in Edit and is in no special mode
        if (doc->getInEdit() && doc->getInEdit()->isDerivedFrom(TechDrawGui::ViewProviderPage::getClassTypeId()))
            return true;
    return false;
}


//===========================================================================
// TechDraw_NewPageDef (default template)
//===========================================================================

DEF_STD_CMD_A(CmdTechDrawNewPageDef);

CmdTechDrawNewPageDef::CmdTechDrawNewPageDef()
  : Command("TechDraw_NewPageDef")
{
    sAppModule      = "TechDraw";
    sGroup          = QT_TR_NOOP("TechDraw");
    sMenuText       = QT_TR_NOOP("Insert new default drawing page");
    sToolTipText    = QT_TR_NOOP("Insert new default drawing page");
    sWhatsThis      = "TechDraw_NewPageDef";
    sStatusTip      = sToolTipText;
    sPixmap         = "actions/techdraw-new-default";
}

void CmdTechDrawNewPageDef::activated(int iMsg)
{
    Base::Reference<ParameterGrp> hGrp = App::GetApplication().GetUserParameter()
        .GetGroup("BaseApp")->GetGroup("Preferences")->GetGroup("Mod/TechDraw");

    std::string defaultDir = App::Application::getResourceDir() + "Mod/TechDraw/Templates/";
    std::string defaultFileName = defaultDir + "A4_LandscapeTD.svg";
    QString templateFileName = QString::fromStdString(hGrp->GetASCII("TemplateFile",defaultFileName.c_str()));
    if (templateFileName.isEmpty()) {
        templateFileName = QString::fromStdString(defaultFileName);
    }

    std::string PageName = getUniqueObjectName("Page");
    std::string TemplateName = getUniqueObjectName("Template");

    QFileInfo tfi(templateFileName);
    if (tfi.isReadable()) {
        Gui::WaitCursor wc;
        openCommand("Drawing create page");
        doCommand(Doc,"App.activeDocument().addObject('TechDraw::DrawPage','%s')",PageName.c_str());
        doCommand(Doc,"App.activeDocument().addObject('TechDraw::DrawSVGTemplate','%s')",TemplateName.c_str());

        doCommand(Doc,"App.activeDocument().%s.Template = '%s'",TemplateName.c_str(), templateFileName.toStdString().c_str());
        doCommand(Doc,"App.activeDocument().%s.Template = App.activeDocument().%s",PageName.c_str(),TemplateName.c_str());

        commitCommand();
        TechDraw::DrawPage* fp = dynamic_cast<TechDraw::DrawPage*>(getDocument()->getObject(PageName.c_str()));
        Gui::ViewProvider* vp = Gui::Application::Instance->getDocument(getDocument())->getViewProvider(fp);
        TechDrawGui::ViewProviderPage* dvp = dynamic_cast<TechDrawGui::ViewProviderPage*>(vp);
        if (dvp) {
            dvp->show();
        }
        else {
            Base::Console().Log("INFO - Template: %s for Page: %s NOT Found\n", PageName.c_str(),TemplateName.c_str());
        }
    } else {
        QMessageBox::critical(Gui::getMainWindow(),
            QLatin1String("No template"),
            QLatin1String("No default template found"));
    }
}

bool CmdTechDrawNewPageDef::isActive(void)
{
    return hasActiveDocument();
}

//===========================================================================
// TechDraw_NewPage (with template choice)
//===========================================================================

DEF_STD_CMD_A(CmdTechDrawNewPage);

CmdTechDrawNewPage::CmdTechDrawNewPage()
  : Command("TechDraw_NewPage")
{
    sAppModule      = "TechDraw";
    sGroup          = QT_TR_NOOP("TechDraw");
    sMenuText       = QT_TR_NOOP("Insert new drawing page from template");
    sToolTipText    = QT_TR_NOOP("Insert new drawing page from template");
    sWhatsThis      = "TechDraw_NewPage";
    sStatusTip      = sToolTipText;
    sPixmap         = "actions/techdraw-new-pick";
}

void CmdTechDrawNewPage::activated(int iMsg)
{
    Base::Reference<ParameterGrp> hGrp = App::GetApplication().GetUserParameter()
        .GetGroup("BaseApp")->GetGroup("Preferences")->GetGroup("Mod/TechDraw");

    std::string defaultDir = App::Application::getResourceDir() + "Mod/TechDraw/Templates";
    QString templateDir = QString::fromStdString(hGrp->GetASCII("TemplateDir", defaultDir.c_str()));
    QString templateFileName = Gui::FileDialog::getOpenFileName(Gui::getMainWindow(),
                                                   QString::fromUtf8(QT_TR_NOOP("Select a Template File")),
                                                   templateDir,
                                                   QString::fromUtf8(QT_TR_NOOP("Template (*.svg *.dxf)")));

    if (templateFileName.isEmpty()) {
        return;
    }

    std::string PageName = getUniqueObjectName("Page");
    std::string TemplateName = getUniqueObjectName("Template");

    QFileInfo tfi(templateFileName);
    if (tfi.isReadable()) {
        Gui::WaitCursor wc;
        openCommand("Drawing create page");
        doCommand(Doc,"App.activeDocument().addObject('TechDraw::DrawPage','%s')",PageName.c_str());

        // Create the Template Object to attach to the page
        doCommand(Doc,"App.activeDocument().addObject('TechDraw::DrawSVGTemplate','%s')",TemplateName.c_str());

        //why is "Template" property set twice? -wf
        // once to set DrawSVGTemplate.Template to OS template file name
        doCommand(Doc,"App.activeDocument().%s.Template = '%s'",TemplateName.c_str(), templateFileName.toStdString().c_str());
        // once to set Page.Template to DrawSVGTemplate.Name
        doCommand(Doc,"App.activeDocument().%s.Template = App.activeDocument().%s",PageName.c_str(),TemplateName.c_str());
        // consider renaming DrawSVGTemplate.Template property?

        commitCommand();
        TechDraw::DrawPage* fp = dynamic_cast<TechDraw::DrawPage*>(getDocument()->getObject(PageName.c_str()));
        Gui::ViewProvider* vp = Gui::Application::Instance->getDocument(getDocument())->getViewProvider(fp);
        TechDrawGui::ViewProviderPage* dvp = dynamic_cast<TechDrawGui::ViewProviderPage*>(vp);
        if (dvp) {
            dvp->show();
        }
        else {
            Base::Console().Log("INFO - Template: %s for Page: %s NOT Found\n", PageName.c_str(),TemplateName.c_str());
        }
    }
    else {
        QMessageBox::critical(Gui::getMainWindow(),
            QLatin1String("No template"),
            QLatin1String("Template file is invalid"));
    }
}

bool CmdTechDrawNewPage::isActive(void)
{
    return hasActiveDocument();
}

//===========================================================================
// TechDraw_NewView
//===========================================================================

DEF_STD_CMD_A(CmdTechDrawNewView);

CmdTechDrawNewView::CmdTechDrawNewView()
  : Command("TechDraw_NewView")
{
    sAppModule      = "TechDraw";
    sGroup          = QT_TR_NOOP("TechDraw");
    sMenuText       = QT_TR_NOOP("Insert view in drawing");
    sToolTipText    = QT_TR_NOOP("Insert a new View of a Part in the active drawing");
    sWhatsThis      = "TechDraw_NewView";
    sStatusTip      = sToolTipText;
    sPixmap         = "actions/techdraw-view";
}

void CmdTechDrawNewView::activated(int iMsg)
{
    TechDraw::DrawPage* page = _findPage(this);
    if (!page) {
        return;
    }

    std::vector<App::DocumentObject*> shapes = getSelection().getObjectsOfType(Part::Feature::getClassTypeId());
    if (shapes.empty()) {
        QMessageBox::warning(Gui::getMainWindow(), QObject::tr("Wrong selection"),
            QObject::tr("Select at least 1 Part object."));
        return;
    }

    std::string PageName = page->getNameInDocument();

    Gui::WaitCursor wc;
    const std::vector<App::DocumentObject*> selectedProjections = getSelection().getObjectsOfType(TechDraw::DrawView::getClassTypeId());

    float newScale = 1.0;
    float newRotation = 0.0;
    Base::Vector3d newDirection(0.0, 0.0, 1.0);
    if (!selectedProjections.empty()) {
        const TechDraw::DrawView* const myView = dynamic_cast<TechDraw::DrawView*>(selectedProjections.front());

        newScale = myView->Scale.getValue();
        newRotation = myView->Rotation.getValue();

        // The "Direction" property does not belong to TechDraw::DrawView, but to one of the
        // many child classes that are projecting objects into the drawing. Therefore, we get the
        // property by name.
        const App::PropertyVector* const propDirection = dynamic_cast<App::PropertyVector*>(myView->getPropertyByName("Direction"));
        if (propDirection) {
            newDirection = propDirection->getValue();
        }
    }

    openCommand("Create view");
    for (std::vector<App::DocumentObject*>::iterator it = shapes.begin(); it != shapes.end(); ++it) {
        std::string FeatName = getUniqueObjectName("View");
        doCommand(Doc,"App.activeDocument().addObject('TechDraw::DrawViewPart','%s')",FeatName.c_str());
        doCommand(Doc,"App.activeDocument().%s.Source = App.activeDocument().%s",FeatName.c_str(),(*it)->getNameInDocument());
        doCommand(Doc,"App.activeDocument().%s.Direction = (%e,%e,%e)",FeatName.c_str(), newDirection.x, newDirection.y, newDirection.z);
        doCommand(Doc,"App.activeDocument().%s.Scale = %e",FeatName.c_str(), newScale);
        doCommand(Doc,"App.activeDocument().%s.Rotation = %e",FeatName.c_str(), newRotation);
        doCommand(Doc,"App.activeDocument().%s.addView(App.activeDocument().%s)",PageName.c_str(),FeatName.c_str());
    }
    updateActive();
    commitCommand();
}

bool CmdTechDrawNewView::isActive(void)
{
    // TODO: Also ensure that there's a part selected?
    return hasActiveDocument();
}

//===========================================================================
// TechDraw_NewViewSection
//===========================================================================

DEF_STD_CMD_A(CmdTechDrawNewViewSection);

CmdTechDrawNewViewSection::CmdTechDrawNewViewSection()
  : Command("TechDraw_NewViewSection")
{
    sAppModule      = "TechDraw";
    sGroup          = QT_TR_NOOP("TechDraw");
    sMenuText       = QT_TR_NOOP("Insert section view in drawing");
    sToolTipText    = QT_TR_NOOP("Insert a new Section View of a Part in the active drawing");
    sWhatsThis      = "TechDraw_NewViewSecton";
    sStatusTip      = sToolTipText;
    sPixmap         = "actions/techdraw-viewsection";
}

void CmdTechDrawNewViewSection::activated(int iMsg)
{
    TechDraw::DrawPage* page = _findPage(this);
    if (!page) {
        return;
    }

    std::vector<App::DocumentObject*> shapes = getSelection().getObjectsOfType(Part::Feature::getClassTypeId());
    if (shapes.empty()) {
        QMessageBox::warning(Gui::getMainWindow(), QObject::tr("Wrong selection"),
            QObject::tr("Select at least 1 Part object."));
        return;
    }
    std::string PageName = page->getNameInDocument();

    Gui::WaitCursor wc;
    openCommand("Create view");
    for (std::vector<App::DocumentObject*>::iterator it = shapes.begin(); it != shapes.end(); ++it) {
        std::string FeatName = getUniqueObjectName("Section");
        doCommand(Doc,"App.activeDocument().addObject('TechDraw::DrawViewSection','%s')",FeatName.c_str());
        doCommand(Doc,"App.activeDocument().%s.Source = App.activeDocument().%s",FeatName.c_str(),(*it)->getNameInDocument());
        doCommand(Doc,"App.activeDocument().%s.addView(App.activeDocument().%s)",PageName.c_str(),FeatName.c_str());
    }
    updateActive();
    commitCommand();
}

bool CmdTechDrawNewViewSection::isActive(void)
{
    // TODO: Also ensure that there's a part selected?
    return hasActiveDocument();
}

//===========================================================================
// TechDraw_ProjGroup
//===========================================================================

DEF_STD_CMD_A(CmdTechDrawProjGroup);

CmdTechDrawProjGroup::CmdTechDrawProjGroup()
  : Command("TechDraw_ProjGroup")
{
    sAppModule      = "TechDraw";
    sGroup          = QT_TR_NOOP("TechDraw");
    sMenuText       = QT_TR_NOOP("Insert Projection Group");
    sToolTipText    = QT_TR_NOOP("Insert 2D Projections of a 3D part into the active drawing");
    sWhatsThis      = "TechDraw_ProjGroup";
    sStatusTip      = sToolTipText;
    sPixmap         = "actions/techdraw-projgroup";
}

void CmdTechDrawProjGroup::activated(int iMsg)
{
    TechDraw::DrawPage* page = _findPage(this);
    if (!page) {
        return;
    }

    std::vector<App::DocumentObject*> shapes = getSelection().getObjectsOfType(Part::Feature::getClassTypeId());
    if (shapes.size() != 1) {
        QMessageBox::warning(Gui::getMainWindow(), QObject::tr("Wrong selection"),
            QObject::tr("Select exactly 1 Part object."));
        return;
    }
    std::string PageName = page->getNameInDocument();

    Gui::WaitCursor wc;

    openCommand("Create Projection Group");
    std::string multiViewName = getUniqueObjectName("cView");
    std::string SourceName = (*shapes.begin())->getNameInDocument();
    doCommand(Doc,"App.activeDocument().addObject('TechDraw::DrawProjGroup','%s')",multiViewName.c_str());
    doCommand(Doc,"App.activeDocument().%s.Source = App.activeDocument().%s",multiViewName.c_str(),SourceName.c_str());

    App::DocumentObject *docObj = getDocument()->getObject(multiViewName.c_str());
    TechDraw::DrawProjGroup *multiView = dynamic_cast<TechDraw::DrawProjGroup *>(docObj);

    // set the anchor
    std::string anchor = "Front";
    doCommand(Doc,"App.activeDocument().%s.addProjection('%s')",multiViewName.c_str(),anchor.c_str());
    doCommand(Doc,"App.activeDocument().%s.Anchor = App.activeDocument().%s.getItemByLabel('%s')",
              multiViewName.c_str(),multiViewName.c_str(),anchor.c_str());

    // create the rest of the desired views
    Gui::Control().showDialog(new TaskDlgProjGroup(multiView));

    // add the multiView to the page
    doCommand(Doc,"App.activeDocument().%s.addView(App.activeDocument().%s)",PageName.c_str(),multiViewName.c_str());

    updateActive();
    commitCommand();
}

bool CmdTechDrawProjGroup::isActive(void)
{
    if ( !hasActiveDocument() || Gui::Control().activeDialog())
        return false;
    return true;
}


//===========================================================================
// TechDraw_Annotation
//===========================================================================

DEF_STD_CMD_A(CmdTechDrawAnnotation);

CmdTechDrawAnnotation::CmdTechDrawAnnotation()
  : Command("TechDraw_Annotation")
{
    // setting the Gui eye-candy
    sGroup        = QT_TR_NOOP("TechDraw");
    sMenuText     = QT_TR_NOOP("&Annotation");
    sToolTipText  = QT_TR_NOOP("Inserts an Annotation in the active drawing");
    sWhatsThis    = "TechDraw_Annotation";
    sStatusTip    = QT_TR_NOOP("Inserts an Annotation in the active drawing");
    sPixmap       = "actions/techdraw-annotation";
}

void CmdTechDrawAnnotation::activated(int iMsg)
{
    TechDraw::DrawPage* page = _findPage(this);
    if (!page) {
        return;
    }
    std::string PageName = page->getNameInDocument();

    std::string FeatName = getUniqueObjectName("Annotation");
    openCommand("Create Annotation");
    doCommand(Doc,"App.activeDocument().addObject('TechDraw::DrawViewAnnotation','%s')",FeatName.c_str());
    doCommand(Doc,"App.activeDocument().%s.addView(App.activeDocument().%s)",PageName.c_str(),FeatName.c_str());
    updateActive();
    commitCommand();
}

bool CmdTechDrawAnnotation::isActive(void)
{
    return hasActiveDocument();
}


//===========================================================================
// TechDraw_Clip
//===========================================================================

DEF_STD_CMD_A(CmdTechDrawClip);

CmdTechDrawClip::CmdTechDrawClip()
  : Command("TechDraw_Clip")
{
    // seting the
    sGroup        = QT_TR_NOOP("TechDraw");
    sMenuText     = QT_TR_NOOP("&Clip");
    sToolTipText  = QT_TR_NOOP("Inserts a clip group in the active drawing");
    sWhatsThis    = "TechDraw_Clip";
    sStatusTip    = QT_TR_NOOP("Inserts a clip group in the active drawing");
    sPixmap       = "actions/techdraw-clip";
}

void CmdTechDrawClip::activated(int iMsg)
{
    TechDraw::DrawPage* page = _findPage(this);
    if (!page) {
        return;
    }
    std::string PageName = page->getNameInDocument();

    std::string FeatName = getUniqueObjectName("Clip");
    openCommand("Create Clip");
    doCommand(Doc,"App.activeDocument().addObject('TechDraw::DrawViewClip','%s')",FeatName.c_str());
    doCommand(Doc,"App.activeDocument().%s.ShowFrame = True",FeatName.c_str());
    doCommand(Doc,"App.activeDocument().%s.Height = 30.0",FeatName.c_str());
    doCommand(Doc,"App.activeDocument().%s.Width = 30.0",FeatName.c_str());
    doCommand(Doc,"App.activeDocument().%s.ShowLabels = False",FeatName.c_str());
    doCommand(Doc,"App.activeDocument().%s.addView(App.activeDocument().%s)",PageName.c_str(),FeatName.c_str());
    updateActive();
    commitCommand();
}

bool CmdTechDrawClip::isActive(void)
{
    return hasActiveDocument();
}

//===========================================================================
// TechDraw_ClipPlus
//===========================================================================

DEF_STD_CMD_A(CmdTechDrawClipPlus);

CmdTechDrawClipPlus::CmdTechDrawClipPlus()
  : Command("TechDraw_ClipPlus")
{
    // seting the
    sGroup        = QT_TR_NOOP("TechDraw");
    sMenuText     = QT_TR_NOOP("&ClipPlus");
    sToolTipText  = QT_TR_NOOP("Add a View to a clip group in the active drawing");
    sWhatsThis    = "TechDraw_ClipPlus";
    sStatusTip    = QT_TR_NOOP("Adds a View into a clip group in the active drawing");
    sPixmap       = "actions/techdraw-clipplus";
}

void CmdTechDrawClipPlus::activated(int iMsg)
{
   std::vector<Gui::SelectionObject> selection = getSelection().getSelectionEx();
   if (selection.size() != 2) {
       QMessageBox::warning(Gui::getMainWindow(), QObject::tr("Wrong selection"),
                            QObject::tr("Select 1 DrawViewClip and 1 DrawView."));
       return;
   }

    TechDraw::DrawViewClip* clip = 0;
    TechDraw::DrawView* view = 0;
    std::vector<Gui::SelectionObject>::iterator itSel = selection.begin();
    for (; itSel != selection.end(); itSel++)  {
        if ((*itSel).getObject()->isDerivedFrom(TechDraw::DrawViewClip::getClassTypeId())) {
            clip = dynamic_cast<TechDraw::DrawViewClip*>((*itSel).getObject());
        } else if ((*itSel).getObject()->isDerivedFrom(TechDraw::DrawView::getClassTypeId())) {
            view = dynamic_cast<TechDraw::DrawView*>((*itSel).getObject());
        }
    }
    if (!view) {
        QMessageBox::warning(Gui::getMainWindow(), QObject::tr("Wrong selection"),
            QObject::tr("Select exactly one Drawing View object."));
        return;
    }
    if (!clip) {
        QMessageBox::warning(Gui::getMainWindow(), QObject::tr("Wrong selection"),
            QObject::tr("Select exactly one Clip object."));
        return;
    }

    TechDraw::DrawPage* pageClip = clip->findParentPage();
    TechDraw::DrawPage* pageView = view->findParentPage();

    if (pageClip != pageView) {
        QMessageBox::warning(Gui::getMainWindow(), QObject::tr("Wrong selection"),
            QObject::tr("Clip and View must be from same Page."));
        return;
    }

    std::string PageName = pageClip->getNameInDocument();
    std::string ClipName = clip->getNameInDocument();
    std::string ViewName = view->getNameInDocument();

    double newX = clip->Width.getValue() / 2.0;
    double newY = clip->Height.getValue() / 2.0;

    openCommand("ClipPlus");
    doCommand(Doc,"App.activeDocument().%s.ViewObject.Visibility = False",ViewName.c_str());
    doCommand(Doc,"App.activeDocument().%s.addView(App.activeDocument().%s)",ClipName.c_str(),ViewName.c_str());
    doCommand(Doc,"App.activeDocument().%s.X = %.3f",ViewName.c_str(),newX);
    doCommand(Doc,"App.activeDocument().%s.Y = %.3f",ViewName.c_str(),newY);
    doCommand(Doc,"App.activeDocument().%s.ViewObject.Visibility = True",ViewName.c_str());
    updateActive();
    commitCommand();
}

bool CmdTechDrawClipPlus::isActive(void)
{
    return hasActiveDocument();
}

//===========================================================================
// TechDraw_ClipMinus
//===========================================================================

DEF_STD_CMD_A(CmdTechDrawClipMinus);

CmdTechDrawClipMinus::CmdTechDrawClipMinus()
  : Command("TechDraw_ClipMinus")
{
    sGroup        = QT_TR_NOOP("TechDraw");
    sMenuText     = QT_TR_NOOP("&ClipMinus");
    sToolTipText  = QT_TR_NOOP("Remove a View from a clip group in the active drawing");
    sWhatsThis    = "TechDraw_ClipMinus";
    sStatusTip    = QT_TR_NOOP("Remove a View from a clip group in the active drawing");
    sPixmap       = "actions/techdraw-clipminus";
}

void CmdTechDrawClipMinus::activated(int iMsg)
{
    std::vector<App::DocumentObject*> dObj = getSelection().getObjectsOfType(TechDraw::DrawView::getClassTypeId());
    if (dObj.empty()) {
        QMessageBox::warning(Gui::getMainWindow(), QObject::tr("Wrong selection"),
                             QObject::tr("Select exactly one Drawing View object."));
        return;
    }
    TechDraw::DrawView* view = dynamic_cast<TechDraw::DrawView*>(dObj.front());

    bool clipFound = false;
    TechDraw::DrawPage* page = view->findParentPage();
    const std::vector<App::DocumentObject*> pViews = page->Views.getValues();
    TechDraw::DrawViewClip* clip = 0;
    for (auto& v:pViews)     {
        clip = nullptr;
        if (v->isDerivedFrom(TechDraw::DrawViewClip::getClassTypeId())) {
            clip = dynamic_cast<TechDraw::DrawViewClip*>(v);
            if (clip->isViewInClip(view)) {
                clipFound = true;
                break;
            }
        }
    }

    if (!clipFound) {
        QMessageBox::warning(Gui::getMainWindow(), QObject::tr("Wrong selection"),
                             QObject::tr("View does not belong to a Clip"));
        return;
    }

    std::string ClipName = clip->getNameInDocument();
    std::string ViewName = view->getNameInDocument();

    openCommand("ClipMinus");
    doCommand(Doc,"App.activeDocument().%s.ViewObject.Visibility = False",ViewName.c_str());
    doCommand(Doc,"App.activeDocument().%s.removeView(App.activeDocument().%s)",ClipName.c_str(),ViewName.c_str());
    doCommand(Doc,"App.activeDocument().%s.ViewObject.Visibility = True",ViewName.c_str());
    updateActive();
    commitCommand();
}

bool CmdTechDrawClipMinus::isActive(void)
{
    return hasActiveDocument();
}


//===========================================================================
// TechDraw_Symbol
//===========================================================================

DEF_STD_CMD_A(CmdTechDrawSymbol);

CmdTechDrawSymbol::CmdTechDrawSymbol()
  : Command("TechDraw_Symbol")
{
    // setting the Gui eye-candy
    sGroup        = QT_TR_NOOP("TechDraw");
    sMenuText     = QT_TR_NOOP("Insert SVG &Symbol");
    sToolTipText  = QT_TR_NOOP("Inserts a symbol from a svg file in the active drawing");
    sWhatsThis    = "TechDraw_Symbol";
    sStatusTip    = QT_TR_NOOP("Inserts a symbol from a svg file in the active drawing");
    sPixmap       = "actions/techdraw-symbol";
}

void CmdTechDrawSymbol::activated(int iMsg)
{
    TechDraw::DrawPage* page = _findPage(this);
    if (!page) {
        return;
    }
    std::string PageName = page->getNameInDocument();

    // Reading an image
    QString filename = Gui::FileDialog::getOpenFileName(Gui::getMainWindow(), QObject::tr("Choose an SVG file to open"), QString::null,
        QString::fromLatin1("%1 (*.svg *.svgz)").arg(QObject::tr("Scalable Vector Graphic")));
    if (!filename.isEmpty())
    {
        std::string FeatName = getUniqueObjectName("Symbol");
        openCommand("Create Symbol");
        doCommand(Doc,"f = open(unicode(\"%s\",'utf-8'),'r')",(const char*)filename.toUtf8());
        doCommand(Doc,"svg = f.read()");
        doCommand(Doc,"f.close()");
        doCommand(Doc,"App.activeDocument().addObject('TechDraw::DrawViewSymbol','%s')",FeatName.c_str());
        doCommand(Doc,"App.activeDocument().%s.Symbol = svg",FeatName.c_str());
        doCommand(Doc,"App.activeDocument().%s.addView(App.activeDocument().%s)",PageName.c_str(),FeatName.c_str());
        updateActive();
        commitCommand();
    }
}

bool CmdTechDrawSymbol::isActive(void)
{
    return hasActiveDocument();
}

//===========================================================================
// TechDraw_DraftView
//===========================================================================

DEF_STD_CMD_A(CmdTechDrawDraftView);

CmdTechDrawDraftView::CmdTechDrawDraftView()
  : Command("TechDraw_DraftView")
{
    // setting the Gui eye-candy
    sGroup        = QT_TR_NOOP("TechDraw");
    sMenuText     = QT_TR_NOOP("Insert a DraftView");
    sToolTipText  = QT_TR_NOOP("Inserts a Draft WB object into the active drawing");
    sWhatsThis    = "TechDraw_DraftView";
    sStatusTip    = QT_TR_NOOP("Inserts a Draft WB object into the active drawing");
    sPixmap       = "actions/techdraw-draft-view";
}

void CmdTechDrawDraftView::activated(int iMsg)
{
    TechDraw::DrawPage* page = _findPage(this);
    if (!page) {
        return;
    }

    std::vector<App::DocumentObject*> shapes = getSelection().getObjectsOfType(Part::Feature::getClassTypeId());
    if (shapes.empty()) {
        QMessageBox::warning(Gui::getMainWindow(), QObject::tr("Wrong selection"),
            QObject::tr("Select at least 1 Part object."));
        return;
    }
    std::string PageName = page->getNameInDocument();
    std::string SourceName = shapes.front()->getNameInDocument();

    for (std::vector<App::DocumentObject*>::iterator it = shapes.begin(); it != shapes.end(); ++it) {
        std::string FeatName = getUniqueObjectName("DraftView");
        std::string SourceName = (*it)->getNameInDocument();
        openCommand("Create DraftView");
        doCommand(Doc,"App.activeDocument().addObject('TechDraw::DrawViewDraft','%s')",FeatName.c_str());
        doCommand(Doc,"App.activeDocument().%s.Source = App.activeDocument().%s",FeatName.c_str(),SourceName.c_str());
        doCommand(Doc,"App.activeDocument().%s.addView(App.activeDocument().%s)",PageName.c_str(),FeatName.c_str());
        updateActive();
        commitCommand();
    }
}

bool CmdTechDrawDraftView::isActive(void)
{
    return hasActiveDocument();
}

//===========================================================================
// TechDraw_Spreadheet
//===========================================================================

DEF_STD_CMD_A(CmdTechDrawSpreadsheet);

CmdTechDrawSpreadsheet::CmdTechDrawSpreadsheet()
  : Command("TechDraw_Spreadsheet")
{
    // seting the
    sGroup        = QT_TR_NOOP("TechDraw");
    sMenuText     = QT_TR_NOOP("Spreadsheet");
    sToolTipText  = QT_TR_NOOP("Inserts a view of a selected spreadsheet into a drawing");
    sWhatsThis    = "TechDraw_Spreadsheet";
    sStatusTip    = QT_TR_NOOP("Inserts a view of a selected spreadsheet into a drawing");
    sPixmap       = "actions/techdraw-spreadsheet";
}

void CmdTechDrawSpreadsheet::activated(int iMsg)
{
    const std::vector<App::DocumentObject*> spreads = getSelection().getObjectsOfType(Spreadsheet::Sheet::getClassTypeId());
    if (spreads.size() != 1) {
        QMessageBox::warning(Gui::getMainWindow(), QObject::tr("Wrong selection"),
            QObject::tr("Select exactly one Spreadsheet object."));
        return;
    }
    std::string SpreadName = spreads.front()->getNameInDocument();

    TechDraw::DrawPage* page = _findPage(this);
    if (!page) {
        return;
    }
    std::string PageName = page->getNameInDocument();

    openCommand("Create spreadsheet view");
    std::string FeatName = getUniqueObjectName("Sheet");
    doCommand(Doc,"App.activeDocument().addObject('TechDraw::DrawViewSpreadsheet','%s')",FeatName.c_str());
    doCommand(Doc,"App.activeDocument().%s.Source = App.activeDocument().%s",FeatName.c_str(),SpreadName.c_str());
    doCommand(Doc,"App.activeDocument().%s.addView(App.activeDocument().%s)",PageName.c_str(),FeatName.c_str());
    updateActive();
    commitCommand();
}

bool CmdTechDrawSpreadsheet::isActive(void)
{
    return (getActiveGuiDocument() ? true : false);
}


//===========================================================================
// TechDraw_ExportPage
//===========================================================================

DEF_STD_CMD_A(CmdTechDrawExportPage);

CmdTechDrawExportPage::CmdTechDrawExportPage()
  : Command("TechDraw_ExportPage")
{
    sGroup        = QT_TR_NOOP("File");
    sMenuText     = QT_TR_NOOP("&Export page...");
    sToolTipText  = QT_TR_NOOP("Export a page to an SVG file");
    sWhatsThis    = "TechDraw_ExportPage";
    sStatusTip    = QT_TR_NOOP("Export a page to an SVG file");
    sPixmap       = "actions/techdraw-saveSVG";
}

void CmdTechDrawExportPage::activated(int iMsg)
{
    TechDraw::DrawPage* page = _findPage(this);
    if (!page) {
        return;
    }
    std::string PageName = page->getNameInDocument();

    Gui::Document* activeGui = Gui::Application::Instance->getDocument(page->getDocument());
    Gui::ViewProvider* vp = activeGui->getViewProvider(page);
    ViewProviderPage* dvp = dynamic_cast<ViewProviderPage*>(vp);

    if (dvp  && dvp->getMDIViewPage()) {
        dvp->getMDIViewPage()->saveSVG();
    } else {
        QMessageBox::warning(Gui::getMainWindow(), QObject::tr("No Drawing View"),
            QObject::tr("Open Drawing View before attempting export to SVG."));
        return;
    }
}

bool CmdTechDrawExportPage::isActive(void)
{
    return hasActiveDocument();
}


void CreateTechDrawCommands(void)
{
    Gui::CommandManager &rcCmdMgr = Gui::Application::Instance->commandManager();

    rcCmdMgr.addCommand(new CmdTechDrawNewPageDef());
    rcCmdMgr.addCommand(new CmdTechDrawNewPage());
    rcCmdMgr.addCommand(new CmdTechDrawNewView());
    rcCmdMgr.addCommand(new CmdTechDrawNewViewSection());
    rcCmdMgr.addCommand(new CmdTechDrawProjGroup());
    rcCmdMgr.addCommand(new CmdTechDrawAnnotation());
    rcCmdMgr.addCommand(new CmdTechDrawClip());
    rcCmdMgr.addCommand(new CmdTechDrawClipPlus());
    rcCmdMgr.addCommand(new CmdTechDrawClipMinus());
    rcCmdMgr.addCommand(new CmdTechDrawSymbol());
    rcCmdMgr.addCommand(new CmdTechDrawExportPage());
    rcCmdMgr.addCommand(new CmdTechDrawDraftView());
    rcCmdMgr.addCommand(new CmdTechDrawSpreadsheet());
}
