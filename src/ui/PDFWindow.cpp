/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * BePDF: The PDF reader for Haiku.
 * 	 Copyright (C) 1997 Benoit Triquet.
 * 	 Copyright (C) 1998-2000 Hubert Figuiere.
 * 	 Copyright (C) 2000-2011 Michael Pfeiffer.
 * 	 Copyright (C) 2013-2016 waddlesplash.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdlib.h>
#include <stdio.h>
#include <Alert.h>
#include <Invoker.h>
#include <ctype.h>

// BeOS
#include <locale/Catalog.h>
#include <be/app/Roster.h>
#include <be/app/MessageQueue.h>
#include <be/interface/Alert.h>
#include <be/interface/Bitmap.h>
#include <be/interface/Button.h>
#include <be/interface/MenuBar.h>
#include <be/interface/MenuItem.h>
#include <be/interface/MenuField.h>
#include <be/interface/Screen.h>
#include <be/interface/ScrollView.h>
#include <be/interface/ScrollBar.h>
#include <be/interface/StringView.h>
#include <be/interface/TabView.h>
#include <be/interface/View.h>
#include <be/storage/FilePanel.h>
#include <be/storage/FindDirectory.h>
#include <be/storage/Path.h>
#include <be/storage/Directory.h>
#include <be/storage/Entry.h>
#include <be/storage/NodeMonitor.h>
#include <be/support/Beep.h>
#include <be/support/Debug.h>
#include <LayoutBuilder.h>

// BePDF
#include "Globals.h"
#include "Application.h"
#include "EntryMenuItem.h"
#include "FileInfoWindow.h"
#include "FindTextWindow.h"
#include "LayoutUtils.h"
#include "AnnotationsView.h"
#include "AttachmentsView.h"
#include "SidebarTabView.h"
#include "OutlinesWindow.h"
#include "Scripting.h"
#include "PageRenderer.h"
#include "PasswordWindow.h"
#include "PDFView.h"
#include "PDFWindow.h"
#include "WebAnnotation.h"
#include "PreferencesWindow.h"
#include "PrintSettingsWindow.h"
#include "ResourceLoader.h"
#include "StatusBar.h"
#include "TraceWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PDFWindow"


char * PDFWindow::PAGE_MSG_LABEL = "page";

// Implementation of RecentDocumentsMenu

RecentDocumentsMenu::RecentDocumentsMenu(const char *title, uint32 what, menu_layout layout)
	: BMenu(title, layout)
	, fWhat(what)
{
}

bool
RecentDocumentsMenu::AddDynamicItem(add_state s)
{
	if (s != B_INITIAL_ADD)
		return false;

	BMenuItem *item;
	BMessage list, *msg;
	entry_ref ref;
	char name[B_FILE_NAME_LENGTH];

	while ((item = RemoveItem((int32)0)) != NULL) {
		delete item;
	}

	be_roster->GetRecentDocuments(&list, 20, NULL, BEPDF_APP_SIG);
	for (int i = 0; list.FindRef("refs", i, &ref) == B_OK; i++) {
		BEntry entry(&ref);
		if (entry.Exists() && entry.GetName(name) == B_OK) {
			msg = new BMessage(fWhat);
			msg->AddRef("refs", &ref);
			item =  new EntryMenuItem(&ref, name, msg, 0, 0);
			AddItem(item);
			if (fWhat == B_REFS_RECEIVED) {
				item->SetTarget(be_app, NULL);
			}
		}
	}

	return false;
}




///////////////////////////////////////////////////////////
/*
	Check errors that may happen
*/
PDFWindow::PDFWindow(entry_ref* ref, BRect frame, const char *ownerPassword,
	const char *userPassword, bool *encrypted)
	: BWindow(frame, "PDF", B_DOCUMENT_WINDOW, 0)
{
	mMainView = NULL;
	mScripting = new Scripting::DocumentHandler(this);
	AddHandler(mScripting);
	mPagesView = NULL;
	mPageNumberItem = NULL;
	mPrintSettings = NULL;
	mTotalPageNumberItem = NULL;
	mFindWindow = NULL;
	mPreferencesItem = NULL;
	mFileInfoItem =  NULL;
	mFindInProgress = false;

	mZoomMenu = mRotationMenu = NULL;
	mLayerView = NULL;
	mAttachmentsView = NULL;
	mAnnotationsView = NULL;
	mSavePanel = NULL;
	mCloseAfterSave = false;
	mChangesKept = false;

	mOWMessenger = NULL;
	mFIWMessenger = NULL;
	mPSWMessenger = NULL;

	mPrintSettingsWindowOpen = false;

	mShowLeftPanel = true;
	mOutlineAutoCollapsed = false;
	mFullScreen = false;

	mPendingMask = 0;


	AddHandler(&mEntryChangedMonitor);
	mEntryChangedMonitor.SetEntryChangedListener(this);

	SetUpViews(ref, ownerPassword, userPassword, encrypted);

	GlobalSettings *settings = gApp->GetSettings();
	int32 ws = settings->GetWorkspace();
	if (settings->GetOpenInWorkspace() && ws >= 1 && ws <= count_workspaces()) {
		SetWorkspaces(1 << (ws - 1));
	}
	mCurrentWorkspace = Workspaces();

	if (mMainView != NULL) {
		mMainView->Redraw();
		InitAfterOpen();
	}

	SetSizeLimits(938, 10000, 131, 10000);
	FitToScreen();
}


///////////////////////////////////////////////////////////
// Makes sure that the window, with its border and title tab, is not larger than the screen and lies on it, for
// the default frame as well as for a frame stored for another screen.
void PDFWindow::FitToScreen()
{
	BScreen screen(this);
	if (!screen.IsValid())
		return;

	const float margin = 10;
	BRect screenFrame = screen.Frame();
	BRect frame = Frame();
	BRect decorator = DecoratorFrame();
	float borderLeft = frame.left - decorator.left, borderTop = frame.top - decorator.top;
	float borderRight = decorator.right - frame.right, borderBottom = decorator.bottom - frame.bottom;

	float maxWidth = screenFrame.Width() - 2 * margin - borderLeft - borderRight;
	float maxHeight = screenFrame.Height() - 2 * margin - borderTop - borderBottom;

	// the minimum size must not stop the window from fitting
	float minWidth, maxWidthLimit, minHeight, maxHeightLimit;
	GetSizeLimits(&minWidth, &maxWidthLimit, &minHeight, &maxHeightLimit);
	SetSizeLimits(min_c(minWidth, maxWidth), maxWidthLimit, min_c(minHeight, maxHeight), maxHeightLimit);

	float width = min_c(frame.Width(), maxWidth);
	float height = min_c(frame.Height(), maxHeight);
	if (width != frame.Width() || height != frame.Height())
		ResizeTo(width, height);

	float left = frame.left, top = frame.top;
	left = max_c(left, screenFrame.left + margin + borderLeft);
	left = min_c(left, screenFrame.right - margin - borderRight - width);
	top = max_c(top, screenFrame.top + margin + borderTop);
	top = min_c(top, screenFrame.bottom - margin - borderBottom - height);
	if (left != frame.left || top != frame.top)
		MoveTo(left, top);
}



///////////////////////////////////////////////////////////
void
PDFWindow::ToolsChanged()
{
	if (mToolBar == NULL || mMainView == NULL)
		return;
	int armed = mMainView->ArmedState();
	mToolBar->SetActionPressed(MARKER_MENU_CMD, armed == 1);
	mToolBar->SetActionPressed(NOTE_BUTTON_CMD, armed == 2);
	mToolBar->SetActionPressed(SHAPES_MENU_CMD, armed == 3);
}


status_t
PDFWindow::GetSupportedSuites(BMessage* data)
{
	Scripting::AddWindowSuite(data);
	return BWindow::GetSupportedSuites(data);
}


BHandler*
PDFWindow::ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier, int32 what, const char* property)
{
	if (Scripting::IsDocumentSpecifier(message, index, specifier, what, property)) {
		// the specifiers that follow are for the document
		message->PopSpecifier();
		return mScripting;
	}
	return BWindow::ResolveSpecifier(message, index, specifier, what, property);
}


PDFWindow::~PDFWindow()
{
	delete mSavePanel;
	RemoveHandler(&mEntryChangedMonitor);

	if (mPagesView) {
		MakeEmpty(mPagesView);
	}
}

void PDFWindow::AnnotationsChanged(int page) {
	if (mAnnotationsView == NULL)
		return;
	if (page > 0)
		mAnnotationsView->RefreshPage(page);
	else
		mAnnotationsView->Refresh();
}

void PDFWindow::TextSizeChanged() {
	TimingMark("text size: window updates");
	SetTotalPageNumber(mMainView->GetNumPages());
	FillPageList();
	TimingMark("text size: page list filled");
	UpdatePageList();
	TimingMark("text size: page list updated");
	// the entries of the outline point to other pages now, and the bookmarks find their places in the text
	SaveUserBookmarks();
	mOutlinesView->Reload(mFileAttributes.GetBookmarks());
	ActivateOutlines();
	TimingMark("text size: outline loaded");
	mAnnotationsView->Refresh();
	SetPage(mMainView->Page());
	UpdateInputEnabler();
	TimingMark("text size: done");
}

void PDFWindow::SetTotalPageNumber(int pages) {
	const char *fmt = B_TRANSLATE("of %d");
	int len = strlen(fmt) + 30;
	char *label = new char[len];
	snprintf (label, len, fmt, pages);
	mTotalPageNumberItem->SetText(label);
	delete[] label;
}

void PDFWindow::InitAfterOpen() {
	GlobalSettings *s = gApp->GetSettings();
	if (Lock()) {
		// set page number text
		SetTotalPageNumber(mMainView->GetNumPages());

		// set window frame
		if (s->GetRestoreWindowFrame()) {
			float left, top;
			mFileAttributes.GetLeftTop(left, top);
			mMainView->ScrollTo(left, top);
		}

		// set page number list
		// the numbers first, the labels (PDF files) when the document is not busy
		FillPageList();
		if (mMainView->GetDocument()->Lock()->LockWithTimeout(0) == B_OK) {
			UpdatePageList();
			mMainView->GetDocument()->Lock()->Unlock();
		} else
			SetPending(UPDATE_PAGE_LIST_PENDING);
		// select page number
		SelectInPageList(mFileAttributes.GetPage());
		if (s->GetRestorePageNumber()) {
			SetZoom(s->GetZoom());
			SetRotation(s->GetRotation());
		}
		Unlock();
	}
	mMainView->HistoryStart();
	TimingMark("open: window initialised");
}


// The pages by their numbers; a book is in chapters, with the pages of a chapter below it.
void PDFWindow::FillPageList() {
	Document* document = mMainView->GetDocument();
	MakeEmpty(mPagesView);
	mPageItems.clear();
	mChapterItems.clear();

	int pages = mMainView->GetNumPages();
	int chapters = document->IsReflowable() ? document->ChapterCount() : 1;
	std::vector<BString> titles;
	if (chapters > 1)
		titles = document->ChapterTitles();

	mPageItems.assign(pages, (PageListItem*)NULL);
	if (chapters > 1) {
		mChapterItems.assign(chapters, (PageListItem*)NULL);
		for (int chapter = 0; chapter < chapters; chapter++) {
			int first = document->ChapterFirstPage(chapter);
			int count = document->ChapterPageCount(chapter);
			BString title = titles[chapter];
			if (title.Length() == 0) {
				title = B_TRANSLATE("Chapter");
				title << " " << chapter + 1;
			}
			PageListItem* chapterItem = new PageListItem(title.String(), 0, first, true);
			mChapterItems[chapter] = chapterItem;
			mPagesView->AddItem(chapterItem);
			// (an item goes right below its parent, so the last page is added first)
			int last = count;
			if (first + last - 1 > pages)
				last = pages - first + 1;
			for (int k = last - 1; k >= 0; k--) {
				BString number;
				number << first + k;
				PageListItem* item = new PageListItem(number.String(), 1, first + k, false);
				mPageItems[first + k - 1] = item;
				mPagesView->AddUnder(item, chapterItem);
			}
			mPagesView->Collapse(chapterItem);
		}
	} else {
		for (int i = 1; i <= pages; i++) {
			BString number;
			number << i;
			PageListItem* item = new PageListItem(number.String(), 0, i, false);
			mPageItems[i - 1] = item;
			mPagesView->AddItem(item);
		}
	}
}


// The labels of the pages of a PDF file, if it has any.
void PDFWindow::UpdatePageList() {
	Document* document = mMainView->GetDocument();
	if (document->IsReflowable())
		return;

	int pages = document->PageCount();
	// the labels need every page to be loaded, which takes a while for really large documents
	if (pages > 2000)
		return;

	std::vector<BString> labels(pages);
	bool hasLabels = false;
	for (int i = 1; i <= pages; i++) {
		BString label = document->PageLabel(i);
		BString number;
		number << i;
		if (label.Length() > 0 && label != number)
			hasLabels = true;
		labels[i - 1] = label.Length() > 0 ? label : number;
	}
	if (!hasLabels)
		return;

	int selected = mPagesView->CurrentSelection();
	PageListItem* current = selected >= 0 ? dynamic_cast<PageListItem*>(mPagesView->ItemAt(selected)) : NULL;
	int page = current != NULL ? current->Page() : 0;

	MakeEmpty(mPagesView);
	mPageItems.assign(pages, (PageListItem*)NULL);
	for (int i = 1; i <= pages; i++) {
		PageListItem* item = new PageListItem(labels[i - 1].String(), 0, i, false);
		mPageItems[i - 1] = item;
		mPagesView->AddItem(item);
	}
	if (page > 0)
		SelectInPageList(page);
}


void PDFWindow::SelectInPageList(int page) {
	if (page < 1 || page > (int)mPageItems.size() || mPageItems[page - 1] == NULL)
		return;

	PageListItem* item = mPageItems[page - 1];
	if (!mChapterItems.empty()) {
		// only the chapter that is read is open
		int chapter = mMainView->GetDocument()->ChapterOfPage(page);
		for (size_t i = 0; i < mChapterItems.size(); i++) {
			if ((int)i == chapter) {
				if (!mChapterItems[i]->IsExpanded())
					mPagesView->Expand(mChapterItems[i]);
			} else if (mChapterItems[i]->IsExpanded())
				mPagesView->Collapse(mChapterItems[i]);
		}
	}
	int32 index = mPagesView->IndexOf(item);
	if (index < 0)
		return;
	// showing the page that is read is not a choice of the user: no message that goes to that page (it arrives late while the
	// view moves on, and takes it back to a page that it has left: flicker, and a link that jumps back)
	mPagesView->SetSelectionMessage(NULL);
	mPagesView->Select(index);
	mPagesView->ScrollToSelection();
	mPagesView->SetSelectionMessage(new BMessage(PAGE_SELECTED_CMD));
}


bool PDFWindow::SetPendingIfLocked(uint32 mask) {
	if (mMainView->GetDocument()->Lock()->LockWithTimeout(0) == B_OK) {
		mMainView->GetDocument()->Lock()->Unlock();
		return false;
	} else {
		// could not lock, schedule action later
		SetPending(mask);
		return true;
	}
}

void PDFWindow::HandlePendingActions(bool ok) {
	if (IsPending(UPDATE_PAGE_LIST_PENDING))
		UpdatePageList();

	if (ok) {
		BMessage msg;
		if (IsPending(UPDATE_OUTLINE_LIST_PENDING)) {
			msg.what = SHOW_BOOKMARKS_CMD;
			MessageReceived(&msg);
		}
		if (IsPending(FILE_INFO_PENDING)) {
			msg.what = FILE_INFO_CMD;
			MessageReceived(&msg);
		}
		if (IsPending(PRINT_SETTINGS_PENDING)) {
			msg.what = PRINT_SETTINGS_CMD;
			MessageReceived(&msg);
		}
	}
	ClearPending();
}

///////////////////////////////////////////////////////////
void PDFWindow::StoreFileAttributes() {
	// store file settings
	if (mMainView && Lock()) {
		entry_ref cur_ref;
		if (mCurrentFile.InitCheck() == B_OK) {
			mCurrentFile.GetRef(&cur_ref);
			// in a book, where the reader stopped is a place in the text
			Document* doc = mMainView->GetDocument();
			BMessage anchorMessage;
			TextAnchor anchor;
			if (doc != NULL && doc->IsReflowable() && !mMainView->IsLayingOut()
				&& doc->MakeAnchor(mMainView->Page(), &anchor))
				anchor.Archive(&anchorMessage);
			mFileAttributes.SetAnchor(anchorMessage.IsEmpty() ? NULL : &anchorMessage);
			mFileAttributes.Write(&cur_ref, gApp->GetSettings());
		}
		Unlock();
	}
}

///////////////////////////////////////////////////////////
bool PDFWindow::QuitRequested() {
	if (!mChangesKept && !ConfirmDiscardChanges(true))
		return false;
	gApp->WindowClosed();
	mAnnotationsView->Stop();
	mMainView->WaitForLayout();
	mMainView->WaitForPage(true);
	StoreFileAttributes();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}

///////////////////////////////////////////////////////////
void PDFWindow::SaveDocument() {
	Document* doc = mMainView->GetDocument();
	if (!doc->HasUnsavedChanges())
		return;
	if (!doc->CanSaveInPlace()) {
		// read-only file, or one that has to be rewritten as a whole: only a copy can be written
		SaveDocumentAs();
		return;
	}
	if (!doc->Save()) {
		BAlert* alert = new BAlert("Error", B_TRANSLATE("The document could not be saved."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
	}
	UpdateInputEnabler();
}


// Asks where to write a copy of the document with its changes.
void PDFWindow::SaveDocumentAs() {
	Document* doc = mMainView->GetDocument();
	if (!doc->IsPDF() && !doc->IsReflowable() && !doc->IsComic())
		return;

	if (mSavePanel == NULL) {
		BMessenger target(this);
		mSavePanel = new BFilePanel(B_SAVE_PANEL, &target, NULL, B_FILE_NODE, false);
	}

	BPath path(doc->Path());
	BString name = path.Leaf();
	BPath directory;
	if (doc->IsWritable()) {
		// next to the original, under another name
		path.GetParent(&directory);
		int32 dot = name.FindLast('.');
		BString suffix(B_TRANSLATE(" (annotated)"));
		if (dot > 0)
			name.Insert(suffix, dot);
		else
			name << suffix;
	} else
		find_directory(B_USER_DIRECTORY, &directory);

	entry_ref ref;
	if (get_ref_for_path(directory.Path(), &ref) == B_OK)
		mSavePanel->SetPanelDirectory(&ref);
	mSavePanel->SetSaveText(name.String());
	mSavePanel->Show();
}


// Writes the copy and goes on with it in this window.
void PDFWindow::SaveCopyTo(const char* path) {
	Document* doc = mMainView->GetDocument();
	if (BString(path) == doc->Path()) {
		// the file itself
		SaveDocument();
		return;
	}

	bool closing = mCloseAfterSave;
	mCloseAfterSave = false;
	mMainView->WaitForPage(true);
	if (!doc->SaveCopy(path)) {
		BAlert* alert = new BAlert("Error", B_TRANSLATE("The copy could not be saved."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		mMainView->Redraw();
		return;
	}

	if (closing) {
		// the window was to be closed, the changes are kept now
		mChangesKept = true;
		PostMessage(B_QUIT_REQUESTED);
		return;
	}

	// show the copy, the original stays as it was
	entry_ref ref;
	if (get_ref_for_path(path, &ref) == B_OK) {
		BMessage open(B_REFS_RECEIVED);
		open.AddRef("refs", &ref);
		be_app->PostMessage(&open);
	}
}


bool PDFWindow::ConfirmDiscardChanges(bool closing) {
	if (mMainView == NULL)
		return true;
	Document* doc = mMainView->GetDocument();
	if (doc == NULL || !doc->HasUnsavedChanges())
		return true;

	bool inPlace = doc->CanSaveInPlace();
	BString text(B_TRANSLATE("The document has unsaved changes (annotations)."));
	if (!inPlace)
		text << "\n" << B_TRANSLATE("They can only be kept in a copy of the file.");
	BAlert* alert = new BAlert("Unsaved", text.String(), B_TRANSLATE("Cancel"), B_TRANSLATE("Discard"),
		inPlace ? B_TRANSLATE("Save") : B_TRANSLATE("Save as" B_UTF8_ELLIPSIS), B_WIDTH_AS_USUAL,
		B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	int32 choice;
#ifdef TOJI_TESTING
	if (getenv("TOJI_AUTOCHOICE") != NULL) {
		choice = atoi(getenv("TOJI_AUTOCHOICE"));
		delete alert;
	} else
#endif
		choice = alert->Go();
	if (choice == 0)
		return false;
	if (choice == 2) {
		if (!inPlace) {
			// the copy is written after the user has chosen a name, then the window closes
			mCloseAfterSave = closing;
			SaveDocumentAs();
			return false;
		}
		return doc->Save();
	}
	return true;
}


///////////////////////////////////////////////////////////
void PDFWindow::CleanUpBeforeLoad() {
}


///////////////////////////////////////////////////////////
bool PDFWindow::IsCurrentFile(entry_ref *ref) const {
	entry_ref r;
	mCurrentFile.GetRef(&r);
	return r == *ref;
}

///////////////////////////////////////////////////////////
bool PDFWindow::LoadFile(entry_ref *ref, const char *ownerPassword, const char *userPassword, bool *encrypted) {
	if (mMainView != NULL) {
		if (!ConfirmDiscardChanges())
			return false;
		mAnnotationsView->Stop();	// it reads the document that is replaced
		StoreFileAttributes();
		CleanUpBeforeLoad();
		// load new file
		if (mMainView->LoadFile(ref, &mFileAttributes, ownerPassword, userPassword, false, encrypted)) {
			mEntryChangedMonitor.StartWatching(ref);
			be_roster->AddToRecentDocuments(ref, BEPDF_APP_SIG);
			mCurrentFile.SetTo(ref);
			InitAfterOpen();
			return true;
		}
	}
	return false;
}

///////////////////////////////////////////////////////////
void PDFWindow::Reload(void) {
    BMessage m(B_REFS_RECEIVED);
    entry_ref ref;
    mCurrentFile.GetRef(&ref);
    m.AddRef("refs", &ref);
	be_app->PostMessage(&m);
}

///////////////////////////////////////////////////////////
void PDFWindow::EntryChanged() {
	Reload();
}

///////////////////////////////////////////////////////////
bool PDFWindow::CancelCommand(BMessage* msg) {
	// While a book is laid out again, nothing that works with its pages can be done.
	if (mMainView != NULL && mMainView->IsLayingOut()) {
		switch (msg->what) {
			case RELOAD_FILE_CMD:
			case SAVE_FILE_CMD:
			case SAVE_AS_FILE_CMD:
			case UNDO_CMD:
			case REDO_CMD:
			case ANNOTATE_HIGHLIGHT_CMD:
			case ANNOTATE_UNDERLINE_CMD:
			case ANNOTATE_STRIKEOUT_CMD:
			case ADD_ANNOTATION_CMD:
			case MARKER_MENU_CMD:
			case ADD_MARGIN_NOTE_CMD:
			case COPY_PLACE_LINK_CMD:
			case SHOW_MARGIN_NOTES_CMD:
			case FANCY_MODE_CMD:
			case NOTE_BUTTON_CMD:
			case SHAPES_MENU_CMD:
			case PRINT_SETTINGS_CMD:
			case PAGESETUP_FILE_CMD:
			case COPY_SELECTION_CMD:
			case SELECT_ALL_CMD:
			case SELECT_NONE_CMD:
			case SET_ZOOM_VALUE_CMD:
			case SET_CUSTOM_ZOOM_FACTOR_CMD:
			case ZOOM_IN_CMD:
			case ZOOM_OUT_CMD:
			case FIT_TO_PAGE_WIDTH_CMD:
			case FIT_TO_PAGE_CMD:
			case FLOW_SINGLE_CMD:
			case FLOW_DOUBLE_CMD:
			case FLOW_CONTINUOUS_CMD:
			case TITLE_PAGE_ALONE_CMD:
			case RIGHT_TO_LEFT_CMD:
			case TOP_TO_BOTTOM_CMD:
			case TEXT_LARGER_CMD:
			case TEXT_SMALLER_CMD:
			case FIRST_PAGE_CMD:
			case NEXT_N_PAGE_CMD:
			case NEXT_PAGE_CMD:
			case PREVIOUS_PAGE_CMD:
			case PREVIOUS_N_PAGE_CMD:
			case LAST_PAGE_CMD:
			case GOTO_PAGE_CMD:
			case SHOW_TARGET_CMD:
			case HISTORY_BACK_CMD:
			case HISTORY_FORWARD_CMD:
			case PAGE_SELECTED_CMD:
			case SET_ROTATE_VALUE_CMD:
			case ROTATE_CLOCKWISE_CMD:
			case ROTATE_ANTI_CLOCKWISE_CMD:
			case FIND_CMD:
			case FIND_NEXT_CMD:
			case FIND_PREVIOUS_CMD:
			case ADD_USER_BOOKMARK_CMD:
			case DELETE_USER_BOOKMARK_CMD:
			case EDIT_USER_BOOKMARK_CMD:
				beep();
				return true;
		}
	}

	// This is a work around:
	// This commands aren't allowed in fullscreen mode, otherwise
	// the windows opened by this commands would be behind the
	// main window and would not block the main window.
	if (mFullScreen) {
		switch(msg->what) {
			case OPEN_FILE_CMD:
			case RELOAD_FILE_CMD:
			case PAGESETUP_FILE_CMD:
			case ABOUT_APP_CMD:
			case FIND_CMD:
			case FIND_NEXT_CMD:
			case FIND_PREVIOUS_CMD:
			case PREFERENCES_FILE_CMD:
			case FILE_INFO_CMD:
			case PRINT_SETTINGS_CMD:
				beep();
				return true;
		}
	}
	return false;
}

///////////////////////////////////////////////////////////
bool PDFWindow::ActivateWindow(BMessenger *messenger) {
	if (messenger && messenger->LockTarget()) {
		BLooper *looper;
		messenger->Target(&looper);
		((BWindow*)looper)->Activate(true);
		looper->Unlock();
		return true;
	} else {
		return false;
	}
}

///////////////////////////////////////////////////////////
bool PDFWindow::CanClose()
{
	return true;
}

void PDFWindow::UpdateInputEnabler()
{
	if (mMainView) {
		Document* doc = mMainView->GetDocument();
		int num_pages = mMainView->GetNumPages();
		int page = mMainView->Page();
		bool b = num_pages > 1 && page != 1;

		fMenuBar->FindItem(FIRST_PAGE_CMD)->SetEnabled(b);
		mToolBar->SetActionEnabled(FIRST_PAGE_CMD, b);
		fMenuBar->FindItem(PREVIOUS_PAGE_CMD)->SetEnabled(b);
		mToolBar->SetActionEnabled(PREVIOUS_N_PAGE_CMD, b);

		b = num_pages > 1 && page != num_pages;
		fMenuBar->FindItem(LAST_PAGE_CMD)->SetEnabled(b);
		mToolBar->SetActionEnabled(LAST_PAGE_CMD, b);
		fMenuBar->FindItem(NEXT_PAGE_CMD)->SetEnabled(b);
		mToolBar->SetActionEnabled(NEXT_PAGE_CMD, b);
		mToolBar->SetActionEnabled(NEXT_N_PAGE_CMD, b);

		mPageNumberItem->SetEnabled(num_pages > 1);

		mToolBar->SetActionEnabled(HISTORY_FORWARD_CMD, mMainView->CanGoForward());
		mToolBar->SetActionEnabled(HISTORY_BACK_CMD, mMainView->CanGoBack());

		int32 dpi = mMainView->GetZoomDPI();
		mToolBar->SetActionEnabled(ZOOM_IN_CMD, dpi != ZOOM_DPI_MAX);
		mToolBar->SetActionEnabled(ZOOM_OUT_CMD, dpi != ZOOM_DPI_MIN);

		mToolBar->SetActionEnabled(FIND_NEXT_CMD, mFindText.Length() > 0);

		int active = mLayerView->Selection();
		mToolBar->SetActionPressed(FULL_SCREEN_CMD, mFullScreen);

		fMenuBar->FindItem(SHOW_PAGE_LIST_CMD)
			->SetMarked(mShowLeftPanel && active == PAGE_LIST_PANEL);
		fMenuBar->FindItem(SHOW_BOOKMARKS_CMD)
			->SetMarked(mShowLeftPanel && active == BOOKMARKS_PANEL);
		fMenuBar->FindItem(SHOW_ATTACHMENTS_CMD)
			->SetEnabled(mAttachmentsView != NULL && mAttachmentsView->Count() > 0);
		fMenuBar->FindItem(SHOW_ATTACHMENTS_CMD)
			->SetMarked(mShowLeftPanel && active == ATTACHMENTS_PANEL);
		PageFlow flow = mMainView->Flow();
		fMenuBar->FindItem(FLOW_SINGLE_CMD)->SetMarked(flow == kFlowSingle);
		fMenuBar->FindItem(FLOW_DOUBLE_CMD)->SetMarked(flow == kFlowDouble);
		fMenuBar->FindItem(FLOW_CONTINUOUS_CMD)->SetMarked(flow == kFlowContinuous);
		// it matters if more than one page is shown at a time
		fMenuBar->FindItem(TITLE_PAGE_ALONE_CMD)->SetMarked(mMainView->TitlePageAlone());
		fMenuBar->FindItem(TITLE_PAGE_ALONE_CMD)->SetEnabled(flow == kFlowDouble || flow == kFlowFourFold);
		// the direction of reading is for comic books
		fMenuBar->FindItem(RIGHT_TO_LEFT_CMD)->SetMarked(mMainView->RightToLeft());
		fMenuBar->FindItem(RIGHT_TO_LEFT_CMD)->SetEnabled(doc->IsComic() && !mMainView->TopToBottom());
		fMenuBar->FindItem(TOP_TO_BOTTOM_CMD)->SetMarked(mMainView->TopToBottom());
		fMenuBar->FindItem(TOP_TO_BOTTOM_CMD)->SetEnabled(doc->IsComic());
		mToolBar->SetActionPressed(FLOW_SINGLE_CMD, flow == kFlowSingle);
		mToolBar->SetActionPressed(FLOW_DOUBLE_CMD, flow == kFlowDouble);
		mToolBar->SetActionPressed(FLOW_CONTINUOUS_CMD, flow == kFlowContinuous);
		fMenuBar->FindItem(SHOW_ANNOTATIONS_CMD)->SetEnabled(doc->CanEditAnnotations());
		fMenuBar->FindItem(SHOW_ANNOTATIONS_CMD)
			->SetMarked(mShowLeftPanel && active == ANNOTATIONS_PANEL);
		mToolBar->SetActionPressed(HIDE_LEFT_PANEL_CMD, mShowLeftPanel);
		fMenuBar->FindItem(HIDE_LEFT_PANEL_CMD)
			->SetLabel(mShowLeftPanel ? B_TRANSLATE("Hide sidebar") : B_TRANSLATE("Show sidebar"));

		fMenuBar->FindItem(OPEN_FILE_CMD)->SetEnabled(!mFullScreen);
		mToolBar->SetActionEnabled(OPEN_FILE_CMD, !mFullScreen);
		fMenuBar->FindItem(RELOAD_FILE_CMD)->SetEnabled(!mFullScreen);
		mToolBar->SetActionEnabled(RELOAD_FILE_CMD, !mFullScreen);
		fMenuBar->FindItem(PRINT_SETTINGS_CMD)
			->SetEnabled(!mFullScreen && !mPrintSettingsWindowOpen && doc->CanPrint());
		mToolBar->SetActionEnabled(PRINT_SETTINGS_CMD,
			!mFullScreen && !mPrintSettingsWindowOpen && doc->CanPrint());

		// PDF security settings
		bool okToCopy = doc->CanCopy();
		fMenuBar->FindItem(COPY_SELECTION_CMD)->SetEnabled(okToCopy);
		fMenuBar->FindItem(SELECT_ALL_CMD)->SetEnabled(okToCopy);
		fMenuBar->FindItem(SELECT_NONE_CMD)->SetEnabled(okToCopy);

		mToolBar->SetActionEnabled(MARKER_MENU_CMD, doc->CanMarkText() && doc->CanCopy());
		mToolBar->SetActionEnabled(NOTE_BUTTON_CMD, doc->CanDrawAnnotations() || doc->CanMarkText());
		mToolBar->SetActionEnabled(SHAPES_MENU_CMD, doc->CanDrawAnnotations());

		bool canMark = doc->CanMarkText() && mMainView->HasTextSelection();
		fMenuBar->FindItem(ANNOTATE_HIGHLIGHT_CMD)->SetEnabled(canMark);
		fMenuBar->FindItem(ANNOTATE_UNDERLINE_CMD)->SetEnabled(canMark);
		fMenuBar->FindItem(ANNOTATE_STRIKEOUT_CMD)->SetEnabled(canMark);
		fMenuBar->FindItem(ADD_MARGIN_NOTE_CMD)->SetEnabled(canMark);
		fMenuBar->FindItem(SHOW_MARGIN_NOTES_CMD)->SetMarked(mMainView->MarginNotesShown());
		fMenuBar->FindItem(FANCY_MODE_CMD)->SetMarked(mMainView->FancyMode());
		fMenuBar->FindItem(SAVE_FILE_CMD)->SetEnabled(doc->HasUnsavedChanges());

		// "Undo Add highlight": what would be undone is named
		BString undo(B_TRANSLATE("Undo")), redo(B_TRANSLATE("Redo"));
		if (doc->CanUndo())
			undo << " " << doc->UndoLabel();
		if (doc->CanRedo())
			redo << " " << doc->RedoLabel();
		fMenuBar->FindItem(UNDO_CMD)->SetLabel(undo.String());
		fMenuBar->FindItem(UNDO_CMD)->SetEnabled(doc->CanUndo());
		fMenuBar->FindItem(REDO_CMD)->SetLabel(redo.String());
		fMenuBar->FindItem(REDO_CMD)->SetEnabled(doc->CanRedo());
		fMenuBar->FindItem(SAVE_AS_FILE_CMD)->SetEnabled(doc->IsPDF() || doc->IsReflowable() || doc->IsComic());
		// shapes, notes and drawings are for pages that stay as they are
		fMenuBar->FindItem(B_TRANSLATE("Add"))->SetEnabled(doc->CanDrawAnnotations());
		fMenuBar->FindItem(TEXT_LARGER_CMD)->SetEnabled(doc->IsReflowable() && doc->TextSize() < Document::kMaxTextSize);
		fMenuBar->FindItem(TEXT_SMALLER_CMD)->SetEnabled(doc->IsReflowable() && doc->TextSize() > Document::kMinTextSize);

		bool hasUserBookmark = mOutlinesView->HasUserBookmark(page);
		bool selected    = hasUserBookmark && mOutlinesView->IsUserBMSelected();
		fMenuBar->FindItem(ADD_USER_BOOKMARK_CMD)->SetEnabled(!hasUserBookmark);
		fMenuBar->FindItem(EDIT_USER_BOOKMARK_CMD)->SetEnabled(selected);
		fMenuBar->FindItem(DELETE_USER_BOOKMARK_CMD)->SetEnabled(selected);
	}
}


void PDFWindow::AddItem(BMenu *subMenu, const char *label, uint32 cmd, bool marked, char shortcut, uint32 modifiers) {
	BMenuItem *item = new BMenuItem(label, new BMessage(cmd), shortcut, modifiers);
	item->SetMarked(marked);
	subMenu->AddItem(item);
}


void PDFWindow::UpdateWindowsMenu() {
/*
	BMenuItem *item;
	while ((item = mWindowsMenu->RemoveItem((int32)0)) != NULL) delete item;
	BList list;
	be_roster->GetAppList(BEPDF_APP_SIG, &list);
	entry_ref ref;
	const int n = list.CountItems();

	for (int i = n-1; i >= 0; i --) {
		team_id who = (team_id)list.ItemAt(i);
		char s[256];
		sprintf(s, "BePDF %d", who);
		mWindowsMenu->AddItem(new BMenuItem(s, NULL));
	}
*/
}


// The icon of the sidebar button: a window with the side bar on the left.
static BBitmap*
MakeSidebarIcon(int size)
{
	BBitmap* bitmap = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32, true);
	BView* view = new BView(bitmap->Bounds(), "sidebar icon", B_FOLLOW_NONE, B_WILL_DRAW);
	bitmap->AddChild(view);
	bitmap->Lock();

	view->SetDrawingMode(B_OP_COPY);
	view->SetHighColor(0, 0, 0, 0);
	view->FillRect(bitmap->Bounds());
	view->SetDrawingMode(B_OP_ALPHA);

	rgb_color paper = { 250, 250, 250, 255 };
	rgb_color ink = tint_color(ui_color(B_PANEL_TEXT_COLOR), B_LIGHTEN_1_TINT);
	rgb_color side = tint_color(ui_color(B_CONTROL_HIGHLIGHT_COLOR), B_LIGHTEN_1_TINT);
	float s = size - 1;
	BRect window(s * 0.08f, s * 0.16f, s * 0.92f, s * 0.84f);
	BRect bar(window.left + 1, window.top + 1, window.left + (window.Width()) * 0.34f, window.bottom - 1);
	view->SetHighColor(paper);
	view->FillRect(window);
	view->SetHighColor(side);
	view->FillRect(bar);
	view->SetHighColor(ink);
	view->StrokeRect(window);
	view->StrokeLine(BPoint(bar.right + 1, window.top), BPoint(bar.right + 1, window.bottom));
	// a few lines in the sidebar and in the page
	for (float y = window.top + 4; y < window.bottom - 2; y += 3) {
		view->StrokeLine(BPoint(bar.left + 2, y), BPoint(bar.right - 2, y));
		view->StrokeLine(BPoint(bar.right + 4, y), BPoint(window.right - 3, y));
	}

	view->Sync();
	bitmap->Unlock();
	bitmap->RemoveChild(view);
	delete view;
	return bitmap;
}


// A small arrow in the corner of a toolbar icon says that the button opens a menu (the colors of the marker, the shapes).
static BBitmap*
WithMenuArrow(BBitmap* icon)
{
	if (icon == NULL)
		return NULL;
	BRect bounds = icon->Bounds();
	BBitmap* bitmap = new BBitmap(bounds, B_RGBA32, true);
	BView* view = new BView(bounds, "menu icon", B_FOLLOW_NONE, B_WILL_DRAW);
	bitmap->AddChild(view);
	bitmap->Lock();

	view->SetDrawingMode(B_OP_COPY);
	view->SetHighColor(0, 0, 0, 0);
	view->FillRect(bounds);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	view->DrawBitmap(icon, BPoint(0, 0));

	// the arrow, drawn in whole pixels (a triangle of 7 by 4)
	view->SetDrawingMode(B_OP_COPY);
	rgb_color light = { 255, 255, 255, 255 };
	rgb_color ink = tint_color(ui_color(B_PANEL_TEXT_COLOR), B_LIGHTEN_1_TINT);
	float centerX = floorf(bounds.right) - 3;
	float top = floorf(bounds.bottom) - 4;
	for (int row = 0; row < 5; row++) {
		// a light row above and around keeps the arrow readable over the icon
		float half = 4 - row;
		view->SetHighColor(light);
		view->StrokeLine(BPoint(centerX - half - 1, top + row - 1), BPoint(centerX + half + 1, top + row - 1));
	}
	for (int row = 0; row < 4; row++) {
		float half = 3 - row;
		view->SetHighColor(ink);
		view->StrokeLine(BPoint(centerX - half, top + row), BPoint(centerX + half, top + row));
	}

	view->Sync();
	bitmap->Unlock();
	bitmap->RemoveChild(view);
	delete view;
	return bitmap;
}


// The icon of a page flow: one page, two side by side, or pages below each other that go on beyond the icon.
static BBitmap*
MakeFlowIcon(PageFlow flow, int size)
{
	BBitmap* bitmap = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32, true);
	BView* view = new BView(bitmap->Bounds(), "flow icon", B_FOLLOW_NONE, B_WILL_DRAW);
	bitmap->AddChild(view);
	bitmap->Lock();

	view->SetDrawingMode(B_OP_COPY);
	view->SetHighColor(0, 0, 0, 0);
	view->FillRect(bitmap->Bounds());
	view->SetDrawingMode(B_OP_ALPHA);

	rgb_color paper = { 250, 250, 250, 255 };
	rgb_color ink = tint_color(ui_color(B_PANEL_TEXT_COLOR), B_LIGHTEN_1_TINT);
	float s = size - 1;
	float pageWidth, pageHeight;
	BRect pages[2];
	int count = 1;
	switch (flow) {
		case kFlowDouble:
			// the spread of a book
			pageWidth = s * 0.40f;
			pageHeight = s * 0.78f;
			pages[0] = BRect(s * 0.08f, s * 0.11f, s * 0.08f + pageWidth, s * 0.11f + pageHeight);
			pages[1] = BRect(s * 0.52f, s * 0.11f, s * 0.52f + pageWidth, s * 0.11f + pageHeight);
			count = 2;
			break;
		case kFlowContinuous:
			// the pages one below the other, the lower one is cut off
			pageWidth = s * 0.54f;
			pages[0] = BRect(s * 0.23f, -2, s * 0.23f + pageWidth, s * 0.50f);
			pages[1] = BRect(s * 0.23f, s * 0.58f, s * 0.23f + pageWidth, s + 2);
			count = 2;
			break;
		default:
			pageWidth = s * 0.56f;
			pageHeight = s * 0.80f;
			pages[0] = BRect(s * 0.22f, s * 0.10f, s * 0.22f + pageWidth, s * 0.10f + pageHeight);
			break;
	}
	for (int i = 0; i < count; i++) {
		view->SetHighColor(paper);
		view->FillRect(pages[i]);
		view->SetHighColor(ink);
		view->StrokeRect(pages[i]);
		// a few lines of text
		BRect lines = pages[i].InsetByCopy(2.5f, 3);
		for (float y = lines.top + 2; y < lines.bottom - 1; y += 3)
			view->StrokeLine(BPoint(lines.left, y), BPoint(lines.right - (((int)y) % 2) * 2, y));
	}

	view->Sync();
	bitmap->Unlock();
	bitmap->RemoveChild(view);
	delete view;
	return bitmap;
}


// the message of an item of the Add menu: the tool the next click or drag works with
static BMessage*
AddToolMessage(PDFView::PlacementTool tool)
{
	BMessage* message = new BMessage(PDFWindow::ADD_ANNOTATION_CMD);
	message->AddInt32("tool", tool);
	return message;
}


BMenuBar* PDFWindow::BuildMenu()
{
	BString label;
	GlobalSettings* settings = gApp->GetSettings();
	int16 zoom = settings->GetZoom();
	float rotation = settings->GetRotation();

	BMenuBar* menuBar = new BMenuBar("mainBar");
	BLayoutBuilder::Menu<>(menuBar)
		.AddMenu(B_TRANSLATE("File"))
			.AddItem(mOpenMenu = new RecentDocumentsMenu(
				B_TRANSLATE("Open" B_UTF8_ELLIPSIS), B_REFS_RECEIVED))
			.AddItem(mNewMenu  = new RecentDocumentsMenu(
				B_TRANSLATE("Open in new window" B_UTF8_ELLIPSIS),
				OPEN_IN_NEW_WINDOW_CMD))
			.AddItem(B_TRANSLATE("Reload"), RELOAD_FILE_CMD, 'R')
			.AddSeparator()
			.AddItem(B_TRANSLATE("Save"), SAVE_FILE_CMD, 'S')
			.AddItem(B_TRANSLATE("Save as" B_UTF8_ELLIPSIS), SAVE_AS_FILE_CMD, 'S', B_SHIFT_KEY)
			.AddSeparator()
			.AddItem(mFileInfoItem = new BMenuItem(B_TRANSLATE("File info" B_UTF8_ELLIPSIS),
				new BMessage(FILE_INFO_CMD), 'I'))
			.AddSeparator()
			.AddItem(B_TRANSLATE("Page setup" B_UTF8_ELLIPSIS), PAGESETUP_FILE_CMD)
			.AddItem(B_TRANSLATE("Print" B_UTF8_ELLIPSIS), PRINT_SETTINGS_CMD, 'P')
			.AddSeparator()
			.AddItem(B_TRANSLATE("Close"), CLOSE_FILE_CMD, 'W')
			.AddItem(B_TRANSLATE("Quit"), QUIT_APP_CMD, 'Q')
		.End()

		.AddMenu(B_TRANSLATE("Edit"))
			.AddItem(B_TRANSLATE("Undo"), UNDO_CMD, 'Z')
			.AddItem(B_TRANSLATE("Redo"), REDO_CMD, 'Z', B_SHIFT_KEY)
			.AddSeparator()
			.AddItem(B_TRANSLATE("Copy"), COPY_SELECTION_CMD, 'C')
			.AddItem(B_TRANSLATE("Copy link to this place"), COPY_PLACE_LINK_CMD, 'L', B_SHIFT_KEY)
			.AddSeparator()
			.AddItem(B_TRANSLATE("Select all"), SELECT_ALL_CMD, 'A')
			.AddItem(B_TRANSLATE("Select none"), SELECT_NONE_CMD, 'A', B_SHIFT_KEY)
			.AddSeparator()
			.AddItem(B_TRANSLATE("Highlight selection"), ANNOTATE_HIGHLIGHT_CMD, 'H', B_SHIFT_KEY)
			.AddItem(B_TRANSLATE("Underline selection"), ANNOTATE_UNDERLINE_CMD, 'U', B_SHIFT_KEY)
			.AddItem(B_TRANSLATE("Strike out selection"), ANNOTATE_STRIKEOUT_CMD, 'K', B_SHIFT_KEY)
			.AddItem(B_TRANSLATE("Add margin note"), ADD_MARGIN_NOTE_CMD, 'N', B_SHIFT_KEY)
			.AddMenu(B_TRANSLATE("Add"))
				.AddItem(B_TRANSLATE("Note"), AddToolMessage(PDFView::kToolNote))
				.AddItem(B_TRANSLATE("Text"), AddToolMessage(PDFView::kToolFreeText))
				.AddItem(B_TRANSLATE("Rectangle"), AddToolMessage(PDFView::kToolRectangle))
				.AddItem(B_TRANSLATE("Ellipse"), AddToolMessage(PDFView::kToolEllipse))
				.AddItem(B_TRANSLATE("Line"), AddToolMessage(PDFView::kToolLine))
				.AddItem(B_TRANSLATE("Arrow"), AddToolMessage(PDFView::kToolArrow))
				.AddItem(B_TRANSLATE("Drawing"), AddToolMessage(PDFView::kToolInk))
			.End()
			.AddSeparator()
			.AddItem(mPreferencesItem = new BMenuItem(B_TRANSLATE("Settings" B_UTF8_ELLIPSIS),
										new BMessage(PREFERENCES_FILE_CMD), ','))
		.End()

		.AddMenu(B_TRANSLATE("View"))
			// the tabs of the sidebar, one key each
			.AddItem(B_TRANSLATE("Show bookmarks"), SHOW_BOOKMARKS_CMD, '1')
			.AddItem(B_TRANSLATE("Show page list"), SHOW_PAGE_LIST_CMD, '2')
			.AddItem(B_TRANSLATE("Show attachments"), SHOW_ATTACHMENTS_CMD, '3')
			.AddItem(B_TRANSLATE("Show annotations"), SHOW_ANNOTATIONS_CMD, '4')
			.AddSeparator()
			// the window and what is around the page, the label says what the item does now
			.AddItem(B_TRANSLATE("Hide sidebar"), HIDE_LEFT_PANEL_CMD, 'H')
			.AddItem(mFullScreenItem = new BMenuItem(B_TRANSLATE("Fullscreen"),
													new BMessage(FULL_SCREEN_CMD), B_RETURN))
			.AddSeparator()
			.AddItem(B_TRANSLATE("Fit to page width"), (FIT_TO_PAGE_WIDTH_CMD), '/')
			.AddItem(B_TRANSLATE("Fit to page"), (FIT_TO_PAGE_CMD), '*')
			.AddSeparator()
			.AddItem(B_TRANSLATE("Single page"), FLOW_SINGLE_CMD)
			.AddItem(B_TRANSLATE("Double-sided"), FLOW_DOUBLE_CMD)
			.AddItem(B_TRANSLATE("Continuous"), FLOW_CONTINUOUS_CMD)
			.AddItem(B_TRANSLATE("Title page alone"), TITLE_PAGE_ALONE_CMD)
			.AddItem(B_TRANSLATE("Right to left"), RIGHT_TO_LEFT_CMD)
			.AddItem(B_TRANSLATE("Top to bottom"), TOP_TO_BOTTOM_CMD)
			.AddItem(B_TRANSLATE("Show margin notes"), SHOW_MARGIN_NOTES_CMD)
			.AddItem(B_TRANSLATE("Fancy page turns"), FANCY_MODE_CMD)
			.AddSeparator()
			.AddItem(B_TRANSLATE("Zoom in"), (ZOOM_IN_CMD), '+')
			.AddItem(B_TRANSLATE("Zoom out"), (ZOOM_OUT_CMD), '-')
			.AddItem(B_TRANSLATE("Larger text"), TEXT_LARGER_CMD, 'T')
			.AddItem(B_TRANSLATE("Smaller text"), TEXT_SMALLER_CMD, 'T', B_SHIFT_KEY)
			.AddSeparator()

			.AddMenu(mZoomMenu = new BMenu(B_TRANSLATE("Zoom")))
				.AddItem("25%", SET_ZOOM_VALUE_CMD, MIN_ZOOM == zoom)
				.AddItem("33%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 1 == zoom)
				.AddItem("50%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 2 == zoom)
				.AddItem("66%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 3 == zoom)
				.AddItem("75%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 4 == zoom)
				.AddItem("100%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 5 == zoom)
				.AddItem("125%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 6 == zoom)
				.AddItem("150%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 7 == zoom)
				.AddItem("175%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 8 == zoom)
				.AddItem("200%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 9 == zoom)
				.AddItem("300%", SET_ZOOM_VALUE_CMD, MIN_ZOOM + 10 == zoom)
			.End()

			.AddSeparator()

			.AddMenu(mRotationMenu = new BMenu(B_TRANSLATE("Rotation")))
				.AddItem("0°", SET_ROTATE_VALUE_CMD, rotation == 0)
				.AddItem("90°", SET_ROTATE_VALUE_CMD, rotation == 90)
				.AddItem("180°", SET_ROTATE_VALUE_CMD, rotation == 180)
				.AddItem("270°", SET_ROTATE_VALUE_CMD, rotation == 270)
			.End()

			.AddSeparator()
			.AddItem(B_TRANSLATE("Show error messages"), SHOW_TRACER_CMD, 'M')
		.End()

		.AddMenu(B_TRANSLATE("Search"))
			.AddItem(B_TRANSLATE("Find" B_UTF8_ELLIPSIS), FIND_CMD, 'F')
			.AddItem(B_TRANSLATE("Find next" B_UTF8_ELLIPSIS), new BMessage(FIND_NEXT_CMD), 'G')
			.AddItem(B_TRANSLATE("Find previous" B_UTF8_ELLIPSIS), new BMessage(FIND_PREVIOUS_CMD), 'G', B_SHIFT_KEY)
		.End()

		.AddMenu(B_TRANSLATE("Page"))
			.AddItem(B_TRANSLATE("First"), FIRST_PAGE_CMD)
			.AddItem(B_TRANSLATE("Previous"), PREVIOUS_PAGE_CMD)
			.AddItem(B_TRANSLATE("Jump to page"), GOTO_PAGE_MENU_CMD, 'J')
			.AddItem(B_TRANSLATE("Next"), NEXT_PAGE_CMD)
			.AddItem(B_TRANSLATE("Last"), LAST_PAGE_CMD)
			.AddSeparator()
			.AddItem(B_TRANSLATE("Back"), HISTORY_BACK_CMD, B_LEFT_ARROW)
			.AddItem(B_TRANSLATE("Forward"), HISTORY_FORWARD_CMD, B_RIGHT_ARROW)
		.End()

		.AddMenu(B_TRANSLATE("Bookmark"))
			.AddItem(B_TRANSLATE("Add"), ADD_USER_BOOKMARK_CMD)
			.AddItem(B_TRANSLATE("Delete"), DELETE_USER_BOOKMARK_CMD)
			.AddItem(B_TRANSLATE("Edit"), EDIT_USER_BOOKMARK_CMD)
		.End()

		.AddMenu(B_TRANSLATE("Help"))
			.AddItem(B_TRANSLATE("Show help" B_UTF8_ELLIPSIS), HELP_CMD)
			.AddItem(B_TRANSLATE("Online help" B_UTF8_ELLIPSIS), ONLINE_HELP_CMD)
			.AddSeparator()
			.AddItem(B_TRANSLATE("Visit homepage" B_UTF8_ELLIPSIS), HOME_PAGE_CMD)
			.AddItem(B_TRANSLATE("Issue tracker" B_UTF8_ELLIPSIS), BUG_REPORT_CMD)
			.AddSeparator()
			.AddItem(B_TRANSLATE("About Toji" B_UTF8_ELLIPSIS), ABOUT_APP_CMD)
		.End();

		mZoomMenu->SetRadioMode (true);
		if (zoom < MIN_ZOOM) SetZoom(zoom);

		mRotationMenu->SetRadioMode(true);

//		menuBar->AddItem ( mWindowsMenu = new BMenu(B_TRANSLATE("Window")) );
		UpdateWindowsMenu();

	mOpenMenu->Superitem()->SetTrigger('O');
	mOpenMenu->Superitem()->SetMessage(new BMessage(OPEN_FILE_CMD));
	mOpenMenu->Superitem()->SetShortcut('O', 0);

	mNewMenu->Superitem()->SetTrigger('N');
	mNewMenu->Superitem()->SetMessage(new BMessage(NEW_WINDOW_CMD));
	mNewMenu->Superitem()->SetShortcut('N', 0);

	return menuBar;
}


BToolBar* PDFWindow::BuildToolBar()
{
	mToolBar = new BToolBar;
	mToolBar->SetName("toolbar");
	mToolBar->SetResizingMode(B_FOLLOW_TOP | B_FOLLOW_LEFT_RIGHT);
	mToolBar->SetFlags(B_WILL_DRAW | B_FRAME_EVENTS);

	mToolBar->AddAction(OPEN_FILE_CMD, this, LoadVectorIcon("OPEN_FILE"),
		B_TRANSLATE("Open file"));
	mToolBar->AddAction(RELOAD_FILE_CMD, this, LoadVectorIcon("RELOAD_FILE"),
		B_TRANSLATE("Reload file"));
	mToolBar->AddAction(PRINT_SETTINGS_CMD, this, LoadVectorIcon("PRINT"),
		B_TRANSLATE("Print"));

	mToolBar->AddSeparator();

	mToolBar->AddAction(HIDE_LEFT_PANEL_CMD, this, MakeSidebarIcon(21), B_TRANSLATE("Show or hide the sidebar"),
		NULL, true);
	mToolBar->AddAction(FULL_SCREEN_CMD, this,
		LoadVectorIcon("FULL_SCREEN"), B_TRANSLATE("Fullscreen mode"),
		NULL, true);

	mToolBar->AddSeparator();

	// marking and annotating: each button arms what the next click or selection works with (Escape lets go)
	mToolBar->AddAction(MARKER_MENU_CMD, this, WithMenuArrow(LoadVectorIcon("ANNOT_HIGHLIGHT")),
		B_TRANSLATE("Marker: choose a color, then select the text"), NULL, true);
	mToolBar->AddAction(NOTE_BUTTON_CMD, this, WithMenuArrow(LoadVectorIcon("ANNOT_NOTE")),
		B_TRANSLATE("Notes: a margin note for selected text, a note or a text on the page"), NULL, true);
	mToolBar->AddAction(SHAPES_MENU_CMD, this, WithMenuArrow(LoadVectorIcon("ART_RECTS")),
		B_TRANSLATE("Text, shapes and drawing: choose one, then click or drag on the page"), NULL, true);

	mToolBar->AddSeparator();

	mToolBar->AddAction(FIRST_PAGE_CMD, this, LoadVectorIcon("FIRST"),
		B_TRANSLATE("Go to start of document"));
	mToolBar->AddAction(PREVIOUS_N_PAGE_CMD, this,
		LoadVectorIcon("PREVIOUS_N"),
		B_TRANSLATE("Go back 10 pages"));
	mToolBar->AddAction(PREVIOUS_PAGE_CMD, this, LoadVectorIcon("PREVIOUS"),
		B_TRANSLATE("Go to previous page"));
	mToolBar->AddAction(NEXT_PAGE_CMD, this, LoadVectorIcon("NEXT"),
		B_TRANSLATE("Go to next page"));
	mToolBar->AddAction(NEXT_N_PAGE_CMD, this, LoadVectorIcon("NEXT_N"),
		B_TRANSLATE("Go forward 10 pages"));
	mToolBar->AddAction(LAST_PAGE_CMD, this, LoadVectorIcon("LAST"),
		B_TRANSLATE("Go to end of document"));

	mToolBar->AddSeparator();

	mToolBar->AddAction(HISTORY_BACK_CMD, this, LoadVectorIcon("BACK"),
		B_TRANSLATE("Back in page history list"));
	mToolBar->AddAction(HISTORY_FORWARD_CMD, this, LoadVectorIcon("FORWARD"),
		B_TRANSLATE("Forward in page history list"));

	mToolBar->AddSeparator();

	// Add "go to page number" TextControl
	mPageNumberItem	= new BTextControl("goto_page",
		"", "", new BMessage(GOTO_PAGE_CMD));
	mPageNumberItem->SetExplicitMaxSize(BSize(50, 25));
	mPageNumberItem->SetAlignment(B_ALIGN_CENTER, B_ALIGN_CENTER);
	mPageNumberItem->SetTarget(this);
	mPageNumberItem->TextView()->DisallowChar(B_ESCAPE);

	BTextView *t = mPageNumberItem->TextView();
	BFont font(be_plain_font);
	t->GetFontAndColor(0, &font);
	font.SetSize(10);
	t->SetFontAndColor(0, 1000, &font, B_FONT_SIZE);
	mToolBar->AddView(mPageNumberItem);

	// display total number of pages
	mTotalPageNumberItem = new BStringView("total_num_of_pages", "");
	mTotalPageNumberItem->SetAlignment(B_ALIGN_CENTER);
	mTotalPageNumberItem->SetFontSize(10);
	mToolBar->AddView(mTotalPageNumberItem);

	mToolBar->AddSeparator();

	mToolBar->AddAction(FIT_TO_PAGE_WIDTH_CMD, this,
		LoadVectorIcon("FIT_TO_PAGE_WIDTH"),
		B_TRANSLATE("Fit to page width"));
	mToolBar->AddAction(FIT_TO_PAGE_CMD, this, LoadVectorIcon("FIT_TO_PAGE"),
		B_TRANSLATE("Fit to page"));

	mToolBar->AddSeparator();

	// how the pages are arranged
	mToolBar->AddAction(FLOW_SINGLE_CMD, this, MakeFlowIcon(kFlowSingle, 21), B_TRANSLATE("Single page"),
		NULL, true);
	mToolBar->AddAction(FLOW_DOUBLE_CMD, this, MakeFlowIcon(kFlowDouble, 21), B_TRANSLATE("Double-sided"),
		NULL, true);
	mToolBar->AddAction(FLOW_CONTINUOUS_CMD, this, MakeFlowIcon(kFlowContinuous, 21),
		B_TRANSLATE("Continuous"), NULL, true);

	mToolBar->AddSeparator();

	mToolBar->AddAction(ROTATE_CLOCKWISE_CMD, this,
		LoadVectorIcon("ROTATE_CLOCKWISE"), B_TRANSLATE("Rotate clockwise"));
	mToolBar->AddAction(ROTATE_ANTI_CLOCKWISE_CMD, this,
		LoadVectorIcon("ROTATE_ANTI_CLOCKWISE"),
		B_TRANSLATE("Rotate counter-clockwise"));
	mToolBar->AddAction(ZOOM_IN_CMD, this, LoadVectorIcon("ZOOM_IN"),
		B_TRANSLATE("Zoom in"));
	mToolBar->AddAction(ZOOM_OUT_CMD, this, LoadVectorIcon("ZOOM_OUT"),
		B_TRANSLATE("Zoom out"));

	mToolBar->AddSeparator();

	mToolBar->AddAction(FIND_CMD, this, LoadVectorIcon("FIND"),
		B_TRANSLATE("Find"));
	mToolBar->AddAction(FIND_NEXT_CMD, this, LoadVectorIcon("FIND_NEXT"),
		B_TRANSLATE("Find next"));
	mToolBar->AddGlue();
	return mToolBar;
}


SidebarTabView* PDFWindow::BuildLeftPanel()
{
	SidebarTabView* layerView = new SidebarTabView("layers");

	// the bookmarks and the outline of the document
	mOutlinesView = new OutlinesView(mMainView->GetDocument(),
		mFileAttributes.GetBookmarks(), gApp->GetSettings(),
		this, B_FRAME_EVENTS);

	// the page numbers
	mPagesView = new BOutlineListView("pagesList", B_SINGLE_SELECTION_LIST,
		B_WILL_DRAW | B_NAVIGABLE | B_FRAME_EVENTS);
	mPagesView->SetSelectionMessage(new BMessage(PAGE_SELECTED_CMD));

	BView *pageView = new BScrollView("pageScrollView", mPagesView,
		B_FRAME_EVENTS, true, true, B_FANCY_BORDER);

	mAttachmentsView = new AttachmentsView(mMainView->GetDocument());
	mAnnotationsView = new AnnotationsView(mMainView->GetDocument(), SHOW_ANNOTATION_CMD);

	// the order is that of BOOKMARKS_PANEL and the others
	layerView->AddPanel(mOutlinesView, B_TRANSLATE("Bookmarks"), LoadVectorIcon("BOOKMARKS", 18));
	layerView->AddPanel(pageView, B_TRANSLATE("Page list"), LoadVectorIcon("SHOW_PAGE_LIST", 18));
	layerView->AddPanel(mAttachmentsView, B_TRANSLATE("Attachments"), LoadVectorIcon("SHOW_ATTACHMENTS", 18));
	layerView->AddPanel(mAnnotationsView, B_TRANSLATE("Annotations"), LoadVectorIcon("SHOW_ANNOT", 18));

	return layerView;
}


void PDFWindow::SetUpViews(entry_ref* ref,
	const char *ownerPassword, const char *userPassword, bool *encrypted)
{
	fMenuBar = BuildMenu();
	BuildToolBar();

	mMainView = new PDFView(ref, &mFileAttributes, "mainView",
		B_WILL_DRAW | B_NAVIGABLE | B_FRAME_EVENTS,
		ownerPassword, userPassword, encrypted);

	mCurrentFile.SetTo(ref);
	if (!mMainView->IsOk()) {
		delete mMainView;
		mMainView = NULL;
		return; // ERROR!
	}
	mEntryChangedMonitor.StartWatching(ref);

	fMainContainer = new BView("ScrollContainer", 0);
	BScrollView* mainScrollView = new BScrollView("scrollView",
		mMainView, 0, true, true, B_FANCY_BORDER);
	mainScrollView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	mainScrollView->SetExplicitMinSize(BSize(0, 0));

	BLayoutBuilder::Group<>(fMainContainer, B_VERTICAL, 0)
		.SetInsets(0, 0, -1, -1)
		.Add(mainScrollView)
	.End();
	fMainContainer->SetExplicitMinSize(BSize(0, 0));

	// left view of SplitView is a LayerView
	mLayerView = BuildLeftPanel();

	// SplitView
	mSplitView = new BSplitView(B_HORIZONTAL);
	// the outline needs room for chapter titles, but the divider must not snap the panel shut or open when it is
	// dragged (the panel is hidden with the command, not by dragging)
	mLayerView->SetExplicitMinSize(
		BSize(be_plain_font->StringWidth("M") * 14, B_SIZE_UNSET));
	// The weights decide how the width is shared (the panel gets about 2/7 of it, and keeps its share when the window
	// grows). The tab view reports the width of its tabs as its largest size, which is less than its smallest.
	mLayerView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	mSplitView->SetCollapsible(false);
	mSplitView->AddChild(mLayerView, 2);
	mSplitView->AddChild(fMainContainer, 5);
	mSplitView->SetInsets(0);
	mSplitView->SetSpacing(4);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0, 0, -1, -1)
		.Add(fMenuBar)
		.Add(mToolBar)
		.Add(mSplitView)
	.End();

	SetTotalPageNumber(mMainView->GetNumPages());

    GlobalSettings *s = gApp->GetSettings();

	// show or hide panel that is stored in settings
	// the annotation and attachment panels of older versions do not exist any more
	ShowLeftPanel(s->GetLeftPanel() == PAGE_LIST_PANEL ? PAGE_LIST_PANEL : BOOKMARKS_PANEL);
	if (!s->GetShowLeftPanel()) {
		// hide panel
		ToggleLeftPanel();
	}
	CollapseOutlinePanelIfEmpty();

	// set focus to PDFView, so it receives mouse and keyboard events
	mMainView->MakeFocus();
}


void PDFWindow::SetZoom(int16 zoom)
{
	BMenuItem *item;
	gApp->GetSettings()->SetZoom(zoom);
	if (zoom >= MIN_ZOOM) {
		item = mZoomMenu->ItemAt(zoom - MIN_ZOOM);
		if (item != NULL)
			item->SetMarked(true);
	} else {
		item = mZoomMenu->FindItem(CUSTOM_ZOOM_FACTOR_MSG);
		if (item != NULL) {
			mZoomMenu->RemoveItem(item);
			delete item;
		}
		char label[256];
		sprintf(label, B_TRANSLATE("Custom zoom factor (%d%%)"), -zoom * 100 / 72);
		BMessage *msg = new BMessage(CUSTOM_ZOOM_FACTOR_MSG);
		msg->AddInt16("zoom", zoom);
		item = new BMenuItem(label, msg, 0);
		mZoomMenu->AddItem(item);
		item->SetMarked(true);
	}
}
///////////////////////////////////////////////////////////
void PDFWindow::SetRotation(float rotation) {
int16 i;
	if (rotation <= 45) i = 0;
	else if (rotation <= 90.0+45) i = 1;
	else if (rotation <= 180.0+45) i = 2;
	else if (rotation <= 270.0+45) i = 3;
	else i = 0;
	BMenuItem *item = mRotationMenu->ItemAt(i);
	item->SetMarked(true);
}

void PDFWindow::NewDoc(Document *doc) {
	TimingMark("open: new document in window");
	mOutlinesView->SetDocument(doc, mFileAttributes.GetBookmarks());
	TimingMark("open: outline loaded");
	mAttachmentsView->SetDocument(doc);
	mAnnotationsView->SetDocument(doc);
	if (mAttachmentsView->Count() == 0 && mLayerView->Selection() == ATTACHMENTS_PANEL)
		ShowLeftPanel(BOOKMARKS_PANEL);
	ActivateOutlines();
	CollapseOutlinePanelIfEmpty();

	if (mFIWMessenger && mFIWMessenger->LockTarget()) {
		BLooper *looper;
		FileInfoWindow *w = (FileInfoWindow*)mFIWMessenger->Target(&looper);
		w->Refresh(&mCurrentFile, doc);
		looper->Unlock();
	}
	if (mPSWMessenger && mPSWMessenger->LockTarget()) {
		BLooper *looper;
		PrintSettingsWindow *w = (PrintSettingsWindow*)mPSWMessenger->Target(&looper);
		w->Refresh(doc);
		looper->Unlock();
	}
}
///////////////////////////////////////////////////////////
void PDFWindow::NewPage(int page) {
	UpdateInputEnabler();
}
///////////////////////////////////////////////////////////
void PDFWindow::FrameMoved(BPoint p) {
	if (!mFullScreen) {
		gApp->GetSettings()->SetWindowPosition(p);
	}
}
///////////////////////////////////////////////////////////
void PDFWindow::FrameResized (float width, float height)
{
	if (!mFullScreen) {
		gApp->GetSettings()->SetWindowSize(width, height);
	}
}


void
PDFWindow::SetZoomSize(float w, float h)
{
	// TODO / FIXME
}

///////////////////////////////////////////////////////////
// update page list and page number item
void
PDFWindow::SetPage(int32 page) {
	char pageStr [64];
    if (page <= 0) page = 1;
    if (page > (int32)mPageItems.size() && !mPageItems.empty()) {
        page = (int32)mPageItems.size();
    }
	snprintf (pageStr, sizeof (pageStr), "%d", (int)page);
	mPageNumberItem->SetText (pageStr);
	SelectInPageList(page);
	mOutlinesView->SelectPage(page);
}


#ifdef TOJI_TESTING
// hey sends numbers as text
static int32
TestInt(BMessage* message, const char* name, int32 fallback)
{
	int32 value;
	if (message->FindInt32(name, &value) == B_OK)
		return value;
	const char* text;
	if (message->FindString(name, &text) == B_OK)
		return atoi(text);
	return fallback;
}
#endif


void
PDFWindow::MessageReceived(BMessage* message)
{
	int32 page;
	const char *text;

	if (CancelCommand(message))
		return;

	switch (message->what) {
	case OPEN_FILE_CMD:
		mMainView->WaitForPage();
		gApp->OpenFilePanel();
		break;
	case NEW_WINDOW_CMD:
		be_roster->Launch(BEPDF_APP_SIG, 0, (char**)NULL);
		break;
	case OPEN_IN_NEW_WINDOW_CMD: {
		BMessage m(B_REFS_RECEIVED);
		entry_ref r;
		if (message->FindRef("refs", 0, &r) == B_OK) {
			BEntry entry(&r);
			BPath path;
			entry.GetPath(&path);
			OpenPDF(path.Path());
		}
		}
		break;
	case RELOAD_FILE_CMD:
		Reload();
		break;
	case SAVE_FILE_CMD:
		SaveDocument();
		break;
	case MARKER_MENU_CMD:
	case NOTE_BUTTON_CMD:
	case SHAPES_MENU_CMD: {
		// a button that is armed puts the tool down; else its menu opens under it
		const int which = message->what == MARKER_MENU_CMD ? 1 : message->what == NOTE_BUTTON_CMD ? 2 : 3;
		if (mMainView->ArmedState() == which) {
			if (mMainView->IsMarkupArmed())
				mMainView->DisarmMarkup();
			else
				mMainView->CancelToolFromToolbar();
		} else if (BButton* button = mToolBar->FindButton(message->what)) {
			BPoint under = button->ConvertToScreen(BPoint(0, button->Bounds().bottom + 1));
			if (which == 1)
				mMainView->ShowMarkerMenu(under);
			else if (which == 2)
				mMainView->ShowNoteMenu(under);
			else
				mMainView->ShowShapesMenu(under);
		}
		ToolsChanged();
		break;
	}
	case ADD_MARGIN_NOTE_CMD:
		mMainView->MarginNoteOnSelection();
		break;
	case COPY_PLACE_LINK_CMD:
		mMainView->CopyPlaceLink();
		break;
	case SHOW_MARGIN_NOTES_CMD:
		mMainView->SetMarginNotesShown(!mMainView->MarginNotesShown());
		break;
	case FANCY_MODE_CMD:
		mMainView->SetFancyMode(!mMainView->FancyMode());
		break;
	case ADD_ANNOTATION_CMD: {
		int32 tool = PDFView::kToolNone;
		message->FindInt32("tool", &tool);
		mMainView->SetTool((PDFView::PlacementTool)tool);
		break;
	}
	case UNDO_CMD:
		mMainView->Undo();
		break;
	case REDO_CMD:
		mMainView->Redo();
		break;
	case SAVE_AS_FILE_CMD:
		SaveDocumentAs();
		break;
	case B_CANCEL:
		mCloseAfterSave = false;	// no name was chosen, the window stays
		break;
	case B_SAVE_REQUESTED: {
		entry_ref directory;
		const char* name;
		if (message->FindRef("directory", &directory) == B_OK && message->FindString("name", &name) == B_OK) {
			BPath path(&directory);
			path.Append(name);
			SaveCopyTo(path.Path());
		}
		break;
	}
	case ANNOTATE_HIGHLIGHT_CMD:
		mMainView->AnnotateSelection(kMarkupHighlight, 0xffeb3b);
		break;
	case ANNOTATE_UNDERLINE_CMD:
		mMainView->AnnotateSelection(kMarkupUnderline, 0xe53935);
		break;
	case ANNOTATE_STRIKEOUT_CMD:
		mMainView->AnnotateSelection(kMarkupStrikeOut, 0xe53935);
		break;
	case CLOSE_FILE_CMD:
		mMainView->WaitForPage(true);
		PostMessage(B_QUIT_REQUESTED);
		break;
	case QUIT_APP_CMD:
	    gApp->Notify(BepdfApplication::NOTIFY_QUIT_MSG);
		break;
	case PAGESETUP_FILE_CMD:
		mMainView->PageSetup ();
		break;
	case ABOUT_APP_CMD:
		be_app->PostMessage(B_ABOUT_REQUESTED);
		break;
	case COPY_SELECTION_CMD: mMainView->CopySelection();
		break;
	case SELECT_ALL_CMD: mMainView->SelectAll();
		break;
	case SELECT_NONE_CMD: mMainView->SelectNone();
		mMainView->ClearFindHighlights();
		break;
	case FIRST_PAGE_CMD:
		mMainView->MoveToPage(1);
		break;
	case PREVIOUS_N_PAGE_CMD:
		mMainView->MoveToPage(mMainView->Page() - 10);
		break;
	case NEXT_N_PAGE_CMD:
		mMainView->MoveToPage(mMainView->Page() + 10);
		break;
	case PREVIOUS_PAGE_CMD:
		if (B_SHIFT_KEY & modifiers()) {
			mMainView->ScrollVertical (false, 0.95);
		} else {
			mMainView->PreviousPage();
		}
		break;
	case NEXT_PAGE_CMD:
		if (B_SHIFT_KEY & modifiers()) {
			mMainView->ScrollVertical (true, 0.95);
		} else {
			mMainView->NextPage();
		}
		break;
	case LAST_PAGE_CMD:
		mMainView->MoveToPage (mMainView->GetNumPages());
		break;
	case SHOW_TARGET_CMD: {
		// a deep link: the target of a Web Annotation, and perhaps what to mark it with
		BMessage target;
		BString motivation;
		if (message->FindMessage("oa:hasTarget", &target) != B_OK
			|| !mMainView->ShowTarget(target, message->FindString("oa:motivatedBy", &motivation) == B_OK))
			beep();
		break;
	}
	case GOTO_PAGE_CMD: {
        status_t result;
        BTextControl * control;
        BControl * ptr;

        result = message->FindPointer ("source", (void **)&ptr);
        if (result == B_OK) {
            control = dynamic_cast <BTextControl *> (ptr);
            if (result == B_OK && control != NULL) {
                const char *txt = control->Text ();
                page = atoi (txt);
            }
        } else {    // may come from external source over page parameter
            result = message->FindInt32("page", &page);
            if (result == B_OK)
                mMainView->WaitForPage();
        }

        if (result == B_OK) {
            LockLooper();
            mMainView->MoveToPage (page);
            UnlockLooper();
            mMainView->MakeFocus();
        }
		break;
    }
	case PAGE_SELECTED_CMD: {
		// a chapter stands for its first page
		int32 selected = mPagesView->CurrentSelection(0);
		PageListItem* item = selected >= 0 ? dynamic_cast<PageListItem*>(mPagesView->ItemAt(selected)) : NULL;
		if (item != NULL)
			mMainView->MoveToPage(item->Page());
		break;
	}
	case GOTO_PAGE_MENU_CMD:
		mPageNumberItem->MakeFocus();
		break;
	case SET_ZOOM_VALUE_CMD: {
			status_t err;
			BMenuItem * item;
			BMenu * menu;
			BArchivable * ptr;
			int32 idx;

			err = message->FindPointer ("source", (void **)&ptr);
			item = dynamic_cast <BMenuItem *> (ptr);
			if (err == B_OK && item != NULL) {
				menu = item->Menu();
				if (menu == NULL) {
					// ERROR
				} else {
					idx = menu->IndexOf(item);
					if (idx > MAX_ZOOM) {
						idx = MAX_ZOOM;
					}
					SetZoom (idx);
					mMainView->SetZoom(idx);
				}
			}
		}
		break;
	case ZOOM_IN_CMD:
	case ZOOM_OUT_CMD:
		mMainView->Zoom(message->what == ZOOM_IN_CMD);
		break;
	case FIT_TO_PAGE_WIDTH_CMD:
		mMainView->FitToPageWidth();
		break;
	case TEXT_LARGER_CMD:
	case TEXT_SMALLER_CMD:
		mMainView->ChangeTextSize(message->what == TEXT_LARGER_CMD);
		break;
	case TITLE_PAGE_ALONE_CMD:
		mMainView->SetTitlePageAlone(!mMainView->TitlePageAlone());
		break;
	case RIGHT_TO_LEFT_CMD:
		mMainView->SetRightToLeft(!mMainView->RightToLeft());
		break;
	case TOP_TO_BOTTOM_CMD:
		mMainView->SetTopToBottom(!mMainView->TopToBottom());
		break;
	case FLOW_SINGLE_CMD:
		mMainView->SetFlow(kFlowSingle);
		break;
	case FLOW_DOUBLE_CMD:
		mMainView->SetFlow(kFlowDouble);
		break;
	case FLOW_CONTINUOUS_CMD:
		mMainView->SetFlow(kFlowContinuous);
		break;
	case FIT_TO_PAGE_CMD:
		mMainView->FitToPage();
		break;
	case SET_ROTATE_VALUE_CMD: {
			status_t err;
			BMenuItem * item;
			BMenu * menu;
			BArchivable * ptr;
			int32 idx;

			err = message->FindPointer ("source", (void **)&ptr);
			item = dynamic_cast <BMenuItem *>(ptr);
			if (err == B_OK && item != NULL) {
				menu = item->Menu();
				if (menu == NULL) {
					// ERROR
				} else {
					idx = menu->IndexOf (item);
					mMainView->SetRotation (idx * 90);
				}
			}
		}
		break;
	case ROTATE_CLOCKWISE_CMD: mMainView->RotateClockwise();
		break;
	case ROTATE_ANTI_CLOCKWISE_CMD: mMainView->RotateAntiClockwise();
		break;
	case HISTORY_BACK_CMD:
		mMainView->Back ();
		break;
	case HISTORY_FORWARD_CMD:
		mMainView->Forward ();
		break;

	case FIND_CMD:
		mMainView->WaitForPage();
		if (Lock()) {
			mFindWindow = new FindTextWindow(gApp->GetSettings(), mFindText.String(), this);
			Unlock();
		}
		break;
	case FIND_NEXT_CMD:
		mMainView->WaitForPage();
		if (Lock()) {
			mFindWindow = new FindTextWindow(gApp->GetSettings(), mFindText.String(), this);
			Unlock();
			mFindWindow->PostMessage('Find');
		}
		break;
	case FIND_PREVIOUS_CMD:
		mMainView->WaitForPage();
		if (Lock()) {
			mFindWindow = new FindTextWindow(gApp->GetSettings(), mFindText.String(), this);
			Unlock();
			mFindWindow->PostMessage(FindTextWindow::FIND_REVERSE_MSG);
		}
		break;
/*	case KEYBOARD_SHORTCUTS_CMD: {
			BAlert *info = new BAlert("Info",
				"Keyboard Shortcuts:\n\n"
				"Space - scroll forward on a page\n"
				"Backspace - scroll backwards on page\n"
				"Cursor Arrow Keys - scroll incrementally in the direction of the cursor key\n"
				"Page Up - skip to the previous page\n"
				"Page Down - skip to the next page\n"
				"Home - return to the beginning of the document\n"
				"End - advance to the end of the document\n"
				"ALT+B - return to the previously viewed page within the document"
				, "OK");
			info->Go();
		}
		break;*/
	case HELP_CMD:
		OpenHelp();
		break;
	case ONLINE_HELP_CMD:
		LaunchHTMLBrowser("https://github.com/sen-laboratories/toji/tree/main/docs/guide");
		break;
	case HOME_PAGE_CMD:
		LaunchHTMLBrowser("https://github.com/sen-laboratories/toji");
		break;
	case BUG_REPORT_CMD:
		LaunchHTMLBrowser("https://github.com/sen-laboratories/toji/issues/");
		break;
	case PREFERENCES_FILE_CMD:
		mPreferencesItem->SetEnabled(false);
		new PreferencesWindow(gApp->GetSettings(), this);
		break;
	case FILE_INFO_CMD:
		if (SetPendingIfLocked(FILE_INFO_PENDING)) return;
		if (!ActivateWindow(mFIWMessenger)) {
			FileInfoWindow *w;
			mMainView->WaitForPage();
			w = new FileInfoWindow(gApp->GetSettings(), &mCurrentFile, mMainView->GetDocument(), this);
			mFIWMessenger = new BMessenger(w);
		}
		break;
	case PRINT_SETTINGS_CMD: {
			if (SetPendingIfLocked(PRINT_SETTINGS_PENDING)) return;
			PrintSettingsWindow *w;
			mPrintSettingsWindowOpen = true;
			UpdateInputEnabler();
			w = new PrintSettingsWindow(mMainView->GetDocument(), gApp->GetSettings(), this);
			mPSWMessenger = new BMessenger(w);
		}
		break;
	case LEGACY_ATTRIBUTES_CMD: {
		BAlert* alert = new BAlert(B_TRANSLATE("Attributes of BePDF"),
			B_TRANSLATE("This file has attributes that BePDF uses for bookmarks and metadata. Toji uses a universal "
				"standard schema for the metadata and Web Annotations for the bookmarks, which BePDF does not know. "
				"The document itself stays as it is.\n\nDo you want to keep the proprietary attributes for use in "
				"BePDF, or upgrade to the standard ones? Upgrading is recommended unless you still use BePDF. "
				"Your choice applies to all files like this, and you can change it later in the settings."),
			B_TRANSLATE("Keep"), B_TRANSLATE("Upgrade"), NULL, B_WIDTH_AS_USUAL,
			B_IDEA_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		alert->ButtonAt(0)->MakeDefault(true);
		alert->Go(new BInvoker(new BMessage(LEGACY_ATTRIBUTES_ANSWER_CMD), this));
		break;
	}
	case LEGACY_ATTRIBUTES_ANSWER_CMD: {
		int32 which = 0;
		message->FindInt32("which", &which);
		gApp->GetSettings()->SetLegacyAttributes(which == 1 ? 2 : 1);
		entry_ref ref;
		if (mCurrentFile.GetRef(&ref) == B_OK)
			BepdfApplication::ApplyLegacyChoice(&ref);
		break;
	}
	case SHOW_BOOKMARKS_CMD:
		if (mShowLeftPanel && mLayerView->Selection() == BOOKMARKS_PANEL)
			HideLeftPanel();
		else
			ShowLeftPanel(BOOKMARKS_PANEL);
		break;
	case SHOW_PAGE_LIST_CMD:
		if (mShowLeftPanel && mLayerView->Selection() == PAGE_LIST_PANEL)
			HideLeftPanel();
		else
			ShowLeftPanel(PAGE_LIST_PANEL);
		break;
	case SHOW_ATTACHMENTS_CMD:
		if (mShowLeftPanel && mLayerView->Selection() == ATTACHMENTS_PANEL)
			HideLeftPanel();
		else
			ShowLeftPanel(ATTACHMENTS_PANEL);
		break;
	case SidebarTabView::kPanelSelected: {
		// the user chose a tab (ShowLeftPanel() selects it itself)
		int32 panel = BOOKMARKS_PANEL;
		message->FindInt32("panel", &panel);
		gApp->GetSettings()->SetLeftPanel(panel);
		if (panel == BOOKMARKS_PANEL)
			ActivateOutlines();
		UpdateInputEnabler();
		break;
	}
	case SHOW_ANNOTATIONS_CMD:
		if (mShowLeftPanel && mLayerView->Selection() == ANNOTATIONS_PANEL)
			HideLeftPanel();
		else
			ShowLeftPanel(ANNOTATIONS_PANEL);
		break;
	case SHOW_ANNOTATION_CMD: {
		// from the list of annotations, or from another application with the id of the annotation
		int32 page = 0, index = -1;
		BString id;
		if (message->FindString("id", &id) == B_OK && id.Length() > 0) {
			id = WebAnnotation::IdentifierKey(id.String());
			int foundPage = 0, foundIndex = -1;
			if (!mMainView->GetDocument()->FindAnnotationById(id.String(), &foundPage, &foundIndex)) {
				beep();
				break;
			}
			page = foundPage;
			index = foundIndex;
		} else {
			message->FindInt32("page", &page);
			message->FindInt32("index", &index);
		}
		mMainView->ShowAnnotation(page, index);
		break;
	}
	case HIDE_LEFT_PANEL_CMD:
		// the item reads "Show sidebar" when it is hidden
		if (mShowLeftPanel)
			HideLeftPanel();
		else
			ShowLeftPanel(mLayerView->Selection());
		break;
	case FULL_SCREEN_CMD: OnFullScreen();
		break;
	case ADD_USER_BOOKMARK_CMD: AddUserBookmark();
		break;
	case DELETE_USER_BOOKMARK_CMD: DeleteUserBookmark();
		break;
	case EDIT_USER_BOOKMARK_CMD: EditUserBookmark();
		break;
	case SHOW_TRACER_CMD: OutputTracer::ShowWindow(gApp->GetSettings());
		break;
	case CUSTOM_ZOOM_FACTOR_MSG: {
		int16 zoom;
		if (message->FindInt16("zoom", &zoom) == B_OK) {
			SetZoom(zoom);
			mMainView->SetZoom(zoom);
		}
		}
		break;

	// Find Text Window
	case FindTextWindow::FIND_START_NOTIFY_MSG: {
			bool ignoreCase;
			bool backward;
			mFindInProgress = true;
			mFindState = (uint32) FindTextWindow::FIND_STOP_NOTIFY_MSG;
			message->FindString("text", &text);
			message->FindBool("ignoreCase", &ignoreCase);
			message->FindBool("backward", &backward);
			mFindText.SetTo(text);
			mMainView->Find(text, ignoreCase, backward, mFindWindow);
			break;
		}
	case FindTextWindow::FIND_STOP_NOTIFY_MSG:
	case FindTextWindow::FIND_ABORT_NOTIFY_MSG:
		if (mFindInProgress) {
			mFindState = message->what;
			mMainView->StopFind();
		} else {
			mFindWindow->PostMessage(message->what);
			if (message->what == (uint32)FindTextWindow::FIND_ABORT_NOTIFY_MSG) {
				mFindWindow->PostMessage(FindTextWindow::FIND_QUIT_REQUESTED_MSG);
			}
		}
		UpdateInputEnabler();
		break;
	case FindTextWindow::TEXT_FOUND_NOTIFY_MSG:
	case FindTextWindow::TEXT_NOT_FOUND_NOTIFY_MSG:
		mFindInProgress = false;
		mFindWindow->PostMessage(mFindState);
		if (mFindState == (uint32)FindTextWindow::FIND_ABORT_NOTIFY_MSG) {
			mFindWindow->PostMessage(FindTextWindow::FIND_QUIT_REQUESTED_MSG);
		}
		break;

	// Page Renderer
	case PageRenderer::FINISH_MSG: {
			thread_id id;
			BBitmap *bitmap;
			PageRenderer::GetParameter(message, &id, &bitmap);
			mMainView->PostRedraw(id, bitmap);
			HandlePendingActions(message->what == PageRenderer::FINISH_MSG);
		}
		break;
	case PageRenderer::ABORT_MSG: {
			thread_id id; BBitmap *bitmap;
			PageRenderer::GetParameter(message, &id, &bitmap);
			mMainView->RedrawAborted(id, bitmap);
			HandlePendingActions(false);
		}
		break;

	// Preferences Window
	case PreferencesWindow::RESTART_DOC_NOTIFY:
		mMainView->WaitForPage(true);
		mMainView->RestartDoc();
		break;
	case PreferencesWindow::CHANGE_NOTIFY: {
		int16 kind, which, index;
			if (PreferencesWindow::DecodeMessage(message, kind, which, index)) {
				switch (kind) {
				case PreferencesWindow::DISPLAY:
					switch (which) {
					case PreferencesWindow::DISPLAY_FILLED_SELECTION:
						mMainView->SetFilledSelection(index == 0);
						break;
					}
				}
			}
		}
		break;
	case PreferencesWindow::QUIT_NOTIFY: mPreferencesItem->SetEnabled(true);
		break;
	case PreferencesWindow::UPDATE_NOTIFY:
		mMainView->UpdateSettings(gApp->GetSettings());
		break;

	// File Info Window
	case FileInfoWindow::QUIT_NOTIFY:
		mFileInfoItem->SetEnabled(true);
		delete mFIWMessenger;
		mFIWMessenger = NULL;
		break;
	// Print Settings Window
	case PrintSettingsWindow::QUIT_NOTIFY:
		// mPrintSettingsItem->SetEnabled(true);
		mPrintSettingsWindowOpen = false;
		UpdateInputEnabler();
		delete mPSWMessenger;
		mPSWMessenger = NULL;
		break;
	case PrintSettingsWindow::PRINT_NOTIFY:
			mMainView->WaitForPage();
			mMainView->Print ();
		break;

	// Outlines View (TODO simplify, BMessenger not needed any more)
	case OutlinesView::PAGE_NOTIFY: {
			int32 page;
			if (message->FindInt32("page", &page) == B_OK) {
				float x, y;
				if (message->FindFloat("x", &x) == B_OK && message->FindFloat("y", &y) == B_OK)
					mMainView->GotoPosition(page, x, y);
				else
					mMainView->MoveToPage(page);
				UpdateInputEnabler();
			}
		}
		break;
	case OutlinesView::QUIT_NOTIFY:
		delete mOWMessenger; mOWMessenger = NULL;
		break;
	case OutlinesView::STATE_CHANGE_NOTIFY:
		UpdateInputEnabler();
		break;
	case BookmarkWindow::BOOKMARK_ENTERED_NOTIFY:
		{
		BString label;
		int32  pageNum;
			if (message->FindString("label", &label) == B_OK &&
			    message->FindInt32("pageNum", &pageNum) == B_OK) {
				// in a book the bookmark is a place in the text, the page changes with the text size
				BMessage anchorMessage;
				TextAnchor anchor;
				Document* doc = mMainView->GetDocument();
				if (doc != NULL && doc->IsReflowable() && doc->MakeAnchor(pageNum, &anchor))
					anchor.Archive(&anchorMessage);
				mOutlinesView->AddUserBookmark(pageNum, label.String(), anchorMessage.IsEmpty() ? NULL : &anchorMessage);
				SaveUserBookmarks();
				UpdateInputEnabler();
			}
		}
		break;

#ifdef TOJI_TESTING
	case 'TSTX': {
			BString cmd;
			message->FindString("cmd", &cmd);
			{
				FILE* out = fopen("/tmp/ts_test.out", "a");
				if (out != NULL) {
					char* type; type_code t; int32 c;
					fprintf(out, "TSTX cmd=[%s]", cmd.String());
					for (int32 i = 0; message->GetInfo(B_ANY_TYPE, i, &type, &t, &c) == B_OK; i++)
						fprintf(out, " %s(%.4s)", type, (char*)&t);
					fputc('\n', out);
					fclose(out);
				}
			}
			if (cmd == "bigwindow") {
				// a frame that does not fit the screen, to see that FitToScreen() makes it fit
				SetSizeLimits(100, 10000, 100, 10000);
				ResizeTo(3000, 2000);
				MoveTo(1500, 900);
				FitToScreen();
				BRect frame = Frame(), decorator = DecoratorFrame(), screen = BScreen(this).Frame();
				FILE* out = fopen("/tmp/ts_test.out", "a");
				if (out != NULL) {
					fprintf(out, "bigwindow: frame %g,%g-%g,%g decorator %g,%g-%g,%g screen %g,%g-%g,%g\n", frame.left,
						frame.top, frame.right, frame.bottom, decorator.left, decorator.top, decorator.right,
						decorator.bottom, screen.left, screen.top, screen.right, screen.bottom);
					fclose(out);
				}
			} else if (cmd == "saveattachment") {
				// index and path, writes the result to the test log
				int32 index = 0;
				BString path;
				index = TestInt(message, "which", 0);
				message->FindString("text", &path);
				bool ok = mMainView->GetDocument()->SaveAttachment(index, path.String());
				FILE* out = fopen("/tmp/ts_test.out", "a");
				if (out != NULL) {
					fprintf(out, "saveattachment %d -> %s: %s\n", (int)index, path.String(), ok ? "ok" : "failed");
					fclose(out);
				}
			} else if (cmd == "chooseannot") {
				// a line of the list of annotations as it is shown (sorted), counted from 0
				mAnnotationsView->TestChoose((int)TestInt(message, "which", 0));
			} else if (cmd == "info") {
				PostMessage(FILE_INFO_CMD);
			} else if (cmd == "savecopy") {
				// as the file panel does, with the path in "text"
				BString text;
				message->FindString("text", &text);
				SaveCopyTo(text.String());
			} else if (cmd == "target") {
				// a deep link as another application sends it: the words ("text"), a page, an EPUB CFI ("cfi")
				BString text, cfi;
				message->FindString("text", &text);
				message->FindString("cfi", &cfi);
				int32 pageNo = TestInt(message, "page", 0);
				BMessage target;
				if (pageNo > 0) {
					BMessage selector;
					BString value;
					value << "page=" << (int)pageNo;
					WebAnnotation::MakeFragmentSelector(&selector, WebAnnotation::kConformsToPdf, value.String());
					target.AddMessage("oa:hasSelector", &selector);
				}
				if (cfi.Length() > 0) {
					BMessage selector;
					WebAnnotation::MakeFragmentSelector(&selector, WebAnnotation::kConformsToEpubCfi, cfi.String());
					target.AddMessage("oa:hasSelector", &selector);
				}
				BString svg, region;
				message->FindString("svg", &svg);
				message->FindString("xywh", &region);
				BString svgFile;
				if (message->FindString("svgfile", &svgFile) == B_OK) {
					// (hey cuts its arguments at the spaces)
					FILE* in = fopen(svgFile.String(), "r");
					if (in != NULL) {
						char buffer[2048];
						size_t got = fread(buffer, 1, sizeof(buffer) - 1, in);
						buffer[got] = '\0';
						svg = buffer;
						fclose(in);
					}
				}
				if (svg.Length() > 0 || region.Length() > 0) {
					// where on the page, as a selector of its own (what refines a page selector is read the same way)
					BMessage selector;
					if (svg.Length() > 0) {
						selector.AddString("type", WebAnnotation::kSvgSelector);
						selector.AddString("rdf:value", svg);
					} else
						WebAnnotation::MakeFragmentSelector(&selector, WebAnnotation::kConformsToMediaFragments,
							region.String());
					target.AddMessage("oa:hasSelector", &selector);
				}
				if (text.Length() > 0) {
					BMessage selector;
					selector.AddString("type", WebAnnotation::kTextQuoteSelector);
					selector.AddString("oa:exact", text);
					target.AddMessage("oa:hasSelector", &selector);
				}
				BMessage show(SHOW_TARGET_CMD);
				show.AddMessage("oa:hasTarget", &target);
				if (TestInt(message, "annotate", 0) != 0)
					show.AddString("oa:motivatedBy", WebAnnotation::kHighlighting);
				MessageReceived(&show);
				FILE* out = fopen("/tmp/ts_test.out", "a");
				if (out != NULL) {
					fprintf(out, "target: unsaved changes %d\n", (int)mMainView->GetDocument()->HasUnsavedChanges());
					fclose(out);
				}
			} else if (cmd == "webannot") {
				// an annotation of the page as JSON-LD, in the test output
				BMessage annotation;
				bool ok = mMainView->GetDocument()->WebAnnotationOf(TestInt(message, "page", mMainView->Page()),
					TestInt(message, "which", 0), &annotation);
				FILE* out = fopen("/tmp/ts_test.out", "a");
				if (out != NULL) {
					fprintf(out, "webannot: %s\n%s", ok ? "ok" : "failed", WebAnnotation::ToJson(annotation).String());
					fclose(out);
				}
			} else if (cmd == "find") {
				// as the find window does it
				if (mFindWindow == NULL)
					mFindWindow = new FindTextWindow(gApp->GetSettings(), "", this);
				BString text;
				message->FindString("text", &text);
				int32 ignoreCase = 1, backward = 0;
				ignoreCase = TestInt(message, "ignoreCase", 1);
				backward = TestInt(message, "backward", 0);
				BMessage start(FindTextWindow::FIND_START_NOTIFY_MSG);
				start.AddString("text", text);
				start.AddBool("ignoreCase", ignoreCase != 0);
				start.AddBool("backward", backward != 0);
				MessageReceived(&start);
			} else if (cmd == "open") {
				// a file is opened in this window, as the file panel or Tracker do it (path in "text")
				BString text;
				message->FindString("text", &text);
				entry_ref ref;
				if (get_ref_for_path(text.String(), &ref) == B_OK) {
					BMessage open(B_REFS_RECEIVED);
					open.AddRef("refs", &ref);
					be_app->PostMessage(&open);
				}
			} else if (cmd == "bookmark") {
				// what the window for a bookmark sends (the page and, in "text", the label)
				BString text;
				message->FindString("text", &text);
				BMessage entered(BookmarkWindow::BOOKMARK_ENTERED_NOTIFY);
				entered.AddString("label", text);
				entered.AddInt32("pageNum", TestInt(message, "page", 1));
				MessageReceived(&entered);
			} else if (cmd == "bookmarkslist") {
				// the bookmarks of the reader, as the sidebar has them
				BMessage list;
				bool ok = mOutlinesView->GetBookmarks(&list);
				FILE* out = fopen("/tmp/ts_test.out", "a");
				if (out != NULL) {
					BString label;
					int32 page = 0;
					fprintf(out, "bookmarks: %s, %d\n", ok ? "ok" : "none", (int)list.CountNames(B_ANY_TYPE));
					for (int32 i = 0; list.FindString("l", i, &label) == B_OK && list.FindInt32("p", i, &page) == B_OK; i++) {
						BMessage anchor;
						list.FindMessage("a", i, &anchor);
						fprintf(out, "  [%s] page %d anchor %s\n", label.String(), (int)page, anchor.IsEmpty() ? "no" : "yes");
					}
					fclose(out);
				}
			} else if (cmd == "splitinfo") {
				FILE* out = fopen("/tmp/ts_test.out", "a");
				if (out != NULL) {
					BView* views[2] = { mLayerView, fMainContainer };
					for (int i = 0; i < 2; i++) {
						fprintf(out, "split item %d: frame %g..%g min %g pref %g max %g weight %g\n", i,
							views[i]->Frame().left, views[i]->Frame().right, views[i]->MinSize().width,
							views[i]->PreferredSize().width, views[i]->MaxSize().width, mSplitView->ItemWeight(i));
					}
					fprintf(out, "split view width %g\n", mSplitView->Bounds().Width());
					fclose(out);
				}
			} else if (cmd.StartsWith("do_")) {
				// any command of the window by name
				static const struct { const char* name; uint32 what; } commands[] = {
					{ "fancy", FANCY_MODE_CMD }, { "fileinfo", FILE_INFO_CMD }, { "righttoleft", RIGHT_TO_LEFT_CMD }, { "toptobottom", TOP_TO_BOTTOM_CMD }, { "undo", UNDO_CMD }, { "redo", REDO_CMD }, { "save", SAVE_FILE_CMD }, { "preferences", PREFERENCES_FILE_CMD }, { "about", ABOUT_APP_CMD },
					{ "printsettings", PRINT_SETTINGS_CMD }, { "rotate", ROTATE_CLOCKWISE_CMD },
					{ "flowsingle", FLOW_SINGLE_CMD }, { "flowdouble", FLOW_DOUBLE_CMD }, { "flowcontinuous", FLOW_CONTINUOUS_CMD }, { "fitwidth", FIT_TO_PAGE_WIDTH_CMD }, { "fitpage", FIT_TO_PAGE_CMD },
					{ "back", HISTORY_BACK_CMD }, { "forward", HISTORY_FORWARD_CMD },
					{ "pagelist", SHOW_PAGE_LIST_CMD }, { "attachments", SHOW_ATTACHMENTS_CMD }, { "annotations", SHOW_ANNOTATIONS_CMD }, { "sidebar", HIDE_LEFT_PANEL_CMD }, { "bookmarks", SHOW_BOOKMARKS_CMD },
					{ "close", CLOSE_FILE_CMD }, { "textlarger", TEXT_LARGER_CMD }, { "textsmaller", TEXT_SMALLER_CMD }, { "zoomin", ZOOM_IN_CMD }, { "zoomout", ZOOM_OUT_CMD }, { "next", NEXT_PAGE_CMD },
					{ "previous", PREVIOUS_PAGE_CMD }, { "last", LAST_PAGE_CMD }, { "first", FIRST_PAGE_CMD },
					{ "markermenu", MARKER_MENU_CMD }, { "shapesmenu", SHAPES_MENU_CMD }, { "notebutton", NOTE_BUTTON_CMD }, { "copy", COPY_SELECTION_CMD }, { "selectall", SELECT_ALL_CMD }, { "addbookmark", ADD_USER_BOOKMARK_CMD },
					{ NULL, 0 }
				};
				BString name(cmd.String() + 3);
				for (int i = 0; commands[i].name != NULL; i++) {
					if (name == commands[i].name) {
						FILE* out = fopen("/tmp/ts_test.out", "a");
						if (out != NULL) {
							fprintf(out, "command %s\n", name.String());
							fclose(out);
						}
						BMessage m(commands[i].what);
						MessageReceived(&m);
					}
				}
			} else
				mMainView->TestCommand(message);
		}
		break;
#endif
	default:
		BWindow::MessageReceived(message);
	}
}


void
PDFWindow::OpenPDF(const char* file)
{
	char *argv[2] = { (char*)file, NULL };
	be_roster->Launch(BEPDF_APP_SIG, 1, argv);
}


bool
PDFWindow::OpenPDFHelp(const char* name)
{
	BPath path(*gApp->GetAppPath());
	path.Append("docs");
	path.Append(name);
	BEntry entry(path.Path());
	if (entry.InitCheck() == B_OK && entry.Exists()) {
		OpenPDF(path.Path());
		return true;
	}
	return false;
}


void
PDFWindow::OpenHelp()
{
	// the user guide that is shipped with the program (built from docs/guide), else the online one
	if (!OpenPDFHelp(B_TRANSLATE_COMMENT("toji-guide.pdf",
			"Replace with the PDF name of the help document, if there is one for your language.")))
		LaunchHTMLBrowser("https://github.com/sen-laboratories/toji/tree/main/docs/guide");
}


void
PDFWindow::LaunchHTMLBrowser(const char *path)
{
	char *argv[2] = {(char*)path, NULL};
	be_roster->Launch("text/html", 1, argv);
}


void
PDFWindow::LaunchInHome(const char *rel_path) {
	BPath path(*gApp->GetAppPath());
	path.Append(rel_path);
	Launch(path.Path());
}

bool
PDFWindow::FindFile(BPath* path) {
	if (path->InitCheck() != B_OK) return false;
	BString leaf(path->Leaf());
	if (leaf.Length() == 0) {
		path->SetTo("/");
		return true;
	}
	if (path->GetParent(path) != B_OK) return false;
	if (FindFile(path)) {
		BPath p(path->Path());
		path->Append(leaf.String());
		BEntry entry(path->Path());
		if (entry.Exists()) return true;

		*path = p;
		entry.SetTo(p.Path());
		BDirectory dir(&entry);
		char name[B_FILE_NAME_LENGTH];
		while (dir.GetNextEntry(&entry) == B_OK) {
			entry.GetName(name);
			if (leaf.ICompare(name) == 0) {
				path->Append(name);
				return true;
			}
		}
	}
	return false;
}

bool
PDFWindow::GetEntryRef(const char* file, entry_ref* ref) {
	BEntry entry(file);
	BPath path(file);
	if (!entry.Exists() && FindFile(&path)) {
		entry.SetTo(path.Path());
	}
	if (entry.Exists()) {
		entry.GetRef(ref); return true;
	}
	return false;
}


void
PDFWindow::Launch(const char *file)
{
	entry_ref r;
	if (GetEntryRef(file, &r)) {
		be_roster->Launch(&r);
	}
}

void
PDFWindow::OpenInWindow(const char *file)
{
	entry_ref r;
	if (GetEntryRef(file, &r)) {
		BMessage msg(B_REFS_RECEIVED);
		msg.AddRef("refs", &r);
		be_app->PostMessage(&msg);
	}
}


// #pragma mark - Left Panel


void
PDFWindow::ActivateOutlines()
{
	// mMainView->WaitForPage();
	if (mLayerView->Selection() == BOOKMARKS_PANEL &&
		mShowLeftPanel) {
		mMainView->WaitForPage();
		mOutlinesView->Activate();
	}
}


void
PDFWindow::CollapseOutlinePanelIfEmpty()
{
	if (mLayerView->Selection() != BOOKMARKS_PANEL)
		return;
	// hidden by the user: leave it alone
	if (!mShowLeftPanel && !mOutlineAutoCollapsed)
		return;

	mMainView->WaitForPage();
	bool empty = !mOutlinesView->HasEntries();
	if (empty == !mShowLeftPanel)
		return;

	// not a user choice, so don't store it in the settings: the panel should be
	// back for the next document that has bookmarks.
	mShowLeftPanel = !empty;
	mOutlineAutoCollapsed = empty;
	mSplitView->SetItemCollapsed(0, empty);
	if (empty)
		mSplitView->SetFlags((~B_NAVIGABLE) & mSplitView->Flags());
	else
		mSplitView->SetFlags(B_NAVIGABLE | mSplitView->Flags());
	UpdateInputEnabler();
	mMainView->Resize();
}


void
PDFWindow::ShowLeftPanel(int panel)
{
	if (!mShowLeftPanel) {
		ToggleLeftPanel();
	}
	if (mLayerView->Selection() != panel) {
		gApp->GetSettings()->SetLeftPanel(panel);
		mLayerView->Select(panel);
	}
	if (panel == BOOKMARKS_PANEL) {
		ActivateOutlines();
	}	
	UpdateInputEnabler();
}


void
PDFWindow::HideLeftPanel()
{
	if (mShowLeftPanel) {
		ToggleLeftPanel();
	}
}


void
PDFWindow::ToggleLeftPanel()
{
	mOutlineAutoCollapsed = false;
	mShowLeftPanel = !mShowLeftPanel;
	mSplitView->SetItemCollapsed(0, !mShowLeftPanel);
	gApp->GetSettings()->SetShowLeftPanel(mShowLeftPanel);
	if (mShowLeftPanel) {
		ActivateOutlines();
		mSplitView->SetFlags(B_NAVIGABLE | mSplitView->Flags());
	} else {
		mSplitView->SetFlags((~B_NAVIGABLE) & mSplitView->Flags());
	}
	UpdateInputEnabler();
	mMainView->Resize();
}


void
PDFWindow::OnFullScreen()
{
	bool quasiFullScreenMode = gApp->GetSettings()->GetQuasiFullscreenMode();
	mFullScreen = !mFullScreen;
	BRect frame;
	if (mFullScreen) {
		mWindowFrame = Frame();
		frame = gScreen->Frame();
		if (quasiFullScreenMode) {
			frame.OffsetBy(0, -fMenuBar->Bounds().Height());
			frame.bottom += fMenuBar->Bounds().Height();
		} else {
			HideLeftPanel();
			BRect bounds = mMainView->Parent()->ConvertToScreen(mMainView->Frame());
			frame.bottom += mWindowFrame.IntegerHeight() - bounds.IntegerHeight();
			frame.right += mWindowFrame.IntegerWidth() - bounds.IntegerWidth();
			frame.OffsetBy(-bounds.left+mWindowFrame.left, -bounds.top+mWindowFrame.top);
		}
		mFullScreenItem->SetMarked(true);
		SetFeel(B_FLOATING_ALL_WINDOW_FEEL);
		SetFlags(Flags() | B_NOT_RESIZABLE | B_NOT_MOVABLE);
		Activate(true);
	} else {
		SetFeel(B_NORMAL_WINDOW_FEEL);
		SetWorkspaces(B_CURRENT_WORKSPACE);
		SetFlags(Flags() & ~(B_NOT_RESIZABLE | B_NOT_MOVABLE));
		frame = mWindowFrame;
		mFullScreenItem->SetMarked(false);
	}
	MoveTo(frame.left, frame.top);
	ResizeTo(frame.Width(), frame.Height());

	UpdateInputEnabler();
}

//~ this is very restrictive: it assumes that the window is only set in one workspace
void PDFWindow::WorkspaceActivated(int32 workspace, bool active) {
#ifdef MORE_DEBUG
	fprintf(stderr, "%s %d %s %d %d\n",
		mFullScreen ? "fullscreen" : "window",
		workspace,
		active ? "active" : "not active",
		mCurrentWorkspace, Workspaces());
#endif
	if (mFullScreen) {
		if (mCurrentWorkspace == 1 << workspace) {
			SetFeel(B_FLOATING_ALL_WINDOW_FEEL);
		} else {
			SetFeel(B_NORMAL_WINDOW_FEEL);
			SetWorkspaces(mCurrentWorkspace);
		}
	} else if (active) {
		mCurrentWorkspace = 1 << workspace;
	}
}

// #pragma mark - User-defined bookmarks

// The name that a new bookmark gets: the title of the section that the page is in, and the page ("7.3 Comic books (p11)"), or
// just "Page 11" if the document has no table of contents. (The pages of a book change with the text size: its bookmarks have
// the title only.)
BString PDFWindow::DefaultBookmarkLabel(int page)
{
	Document* doc = mMainView->GetDocument();
	std::vector<DocOutlineEntry> entries;
	const DocOutlineEntry* section = NULL;
	if (doc != NULL && doc->LoadOutline(entries)) {
		// the last entry that starts on or before the page is the one that the page is in
		for (size_t i = 0; i < entries.size(); i++) {
			if (entries[i].page > 0 && entries[i].page <= page && entries[i].title.Length() > 0)
				section = &entries[i];
		}
	}
	BString label;
	char buffer[16];
	snprintf(buffer, sizeof(buffer), "%d", page);
	if (section != NULL && doc->IsReflowable()) {
		label = section->title;
	} else if (section != NULL) {
		label = B_TRANSLATE_COMMENT("%1 (p%2)", "Name of a bookmark: the section (1) and the page (2)");
		label.ReplaceFirst("%1", section->title.String());
		label.ReplaceFirst("%2", buffer);
	} else {
		char text[64];
		snprintf(text, sizeof(text), B_TRANSLATE("Page %d"), page);
		label = text;
	}
	return label;
}

void PDFWindow::AddUserBookmark()
{
	BString label = DefaultBookmarkLabel(mMainView->Page());
	new BookmarkWindow(mMainView->Page(), label.String(), BRect(30, 30, 300, 200), this);
}

void PDFWindow::DeleteUserBookmark() {
	mOutlinesView->RemoveUserBookmark(mMainView->Page());
	SaveUserBookmarks();
	UpdateInputEnabler();
}

void PDFWindow::EditUserBookmark() {
	const char *label = mOutlinesView->GetUserBMLabel(mMainView->Page());
	if (label) {
		new BookmarkWindow(mMainView->Page(), label, BRect(30, 30, 300, 200), this);
	} else {
		// should not reach here
	}
}

void PDFWindow::SaveUserBookmarks()
{
	if (mCurrentFile.InitCheck() != B_OK)
		return;
	
	BMessage bm;
	if (!mOutlinesView->GetBookmarks(&bm))
		return;
	
	entry_ref cur_ref;
	mCurrentFile.GetRef(&cur_ref);
	
	mFileAttributes.SetBookmarks(&bm);
	mFileAttributes.Write(&cur_ref, gApp->GetSettings());
}
