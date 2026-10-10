/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * BePDF: The PDF reader for Haiku.
 * 	 Copyright (C) 1997 Benoit Triquet.
 * 	 Copyright (C) 1998-2000 Hubert Figuiere.
 * 	 Copyright (C) 2000-2011 Michael Pfeiffer.
 * 	 Copyright (C) 2013 waddlesplash.
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


#include <stdarg.h>
#include <stdio.h>
#include <math.h>

// BeOS
#include <locale/Catalog.h>

#include <algorithm>
#include <math.h>

#include <be/app/Application.h>
#include <be/app/Clipboard.h>
#include <be/app/Looper.h>
#include <be/app/MessageRunner.h>
#include <be/app/MessageQueue.h>
#include <be/app/Roster.h>

#include <be/interface/Button.h>
#include <be/interface/ScrollBar.h>
#include <be/interface/PrintJob.h>
#include <be/interface/Alert.h>
#include <be/interface/Region.h>
#include <be/interface/StringView.h>
#include <be/interface/PopUpMenu.h>
#include <be/interface/MenuItem.h>

#include <be/storage/Path.h>
#include <be/storage/Entry.h>
#include <be/storage/Directory.h>
#include <be/storage/File.h>
#include <be/storage/NodeInfo.h>
#include <be/translation/BitmapStream.h>
#include <be/translation/TranslatorFormats.h>
#include <be/translation/TranslationUtils.h>
#include <be/translation/TranslatorRoster.h>
#include <be/support/String.h>
#include <be/support/Debug.h>
#include <be/support/Beep.h>

// BePDF
#include "Globals.h"
#include "Application.h"
#include "CachedPage.h"
#include "FileInfoWindow.h"
#include "FindTextWindow.h"
#include "NoteWindow.h"
#include <MessageRunner.h>
#include <Path.h>
#include <SimpleGameSound.h>
#include "PageRenderer.h"
#include "PDFWindow.h"
#include "BusyWindow.h"
#include "ComicInfo.h"
#include "EpubCfi.h"
#include "WebAnnotation.h"
#include "EpubInfo.h"
#include "PDFView.h"
#include "DeepLink.h"
#include "PrintingProgressWindow.h"
#include "ResourceLoader.h"
#include "StatusWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PDFView"


// zoom factor is 1.2 (similar to DVI magsteps)
static const int kZoomDPI[MAX_ZOOM - MIN_ZOOM + 1] = {
	18, 24, 36, 48, 54,
	72,
	90, 108, 127, 144, 216
};

#define OPEN_FILE_MSG                  'open'
#define COPY_LINK_MSG                  'cplk'
#define COPY_SELECTION_MSG             'cpsl'
#define SELECT_ALL_MSG                 'slal'
#define ANNOTATE_MSG                   'anno'
#define DELETE_ANNOTATION_MSG          'dlan'
#define COPY_ANNOTATION_MSG            'cpan'
#define EDIT_NOTE_MSG                  'ednt'
#define NOTE_ENTERED_MSG               'ntnt'
#define CHANGE_COLOR_MSG               'chcl'
#define CHANGE_STYLE_MSG               'chst'
#define COPY_PLACE_LINK_MSG            'cpPl'
#define ADD_TOOL_MSG                   'adtl'
#define ARM_MARKUP_MSG                 'armM'
#define NOTE_MARGIN_MSG                'mgnn'
#define CREATE_TEXT_MSG                'crtx'
#define MODIFIERS_POLL_MSG             'mdfy'
#define SHOW_BUSY_MSG                  'busy'
#define TARGET_DONE_MSG                'tgtD'
#define LAYOUT_DONE_MSG                'lyDn'
#define PAGE_TURN_TICK_MSG             'pgTk'
#define PAGE_TURN_TIMEOUT_MSG          'pgTO'

static bool SelectModifierDown();

// how long the pointer rests on a note until a click edits it (about the delay of the tooltip)
static const bigtime_t kNoteEditDelay = 600000;


// the colors offered for marking text
static const struct { const char* name; uint32 rgb; } kMarkerColors[] = {
	{ B_TRANSLATE_MARK("Yellow"), 0xffeb3b }, { B_TRANSLATE_MARK("Green"), 0x96e678 },
	{ B_TRANSLATE_MARK("Blue"), 0x78beff }, { B_TRANSLATE_MARK("Pink"), 0xff96c8 },
	{ B_TRANSLATE_MARK("Orange"), 0xffb95a }, { B_TRANSLATE_MARK("Red"), 0xe53935 }
};
static const int kMarkerColorCount = sizeof(kMarkerColors) / sizeof(kMarkerColors[0]);

// the color around the page, a little darker than the panels of the system
static rgb_color
DesktopColor()
{
	return tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_3_TINT);
}

static const float kGap = 6;

// A menu item with a box of a color in front of the label, to the right of the check mark.
class ColorMenuItem : public BMenuItem {
public:
	ColorMenuItem(const char* label, uint32 rgb, BMessage* message)
		:
		BMenuItem(label, message),
		fColor(rgb)
	{
	}

	virtual void GetContentSize(float* width, float* height)
	{
		BMenuItem::GetContentSize(width, height);
		*width += BoxWidth() + kGap;
	}

	virtual void DrawContent()
	{
		BMenu* menu = Menu();
		BPoint origin = menu->PenLocation();
		font_height fontHeight;
		menu->GetFontHeight(&fontHeight);
		float height = ceilf(fontHeight.ascent + fontHeight.descent);

		BRect box(origin.x, origin.y + 1, origin.x + BoxWidth() - 1, origin.y + height - 2);
		rgb_color color = { (uint8)(fColor >> 16), (uint8)(fColor >> 8), (uint8)fColor, 255 };
		rgb_color saved = menu->HighColor();
		menu->SetHighColor(color);
		menu->FillRect(box);
		menu->SetHighColor(tint_color(ui_color(B_MENU_BACKGROUND_COLOR), B_DARKEN_3_TINT));
		menu->StrokeRect(box);
		menu->SetHighColor(saved);

		menu->MovePenTo(origin.x + BoxWidth() + kGap, origin.y);
		BMenuItem::DrawContent();
	}

private:
	float BoxWidth() const
	{
		return ceilf(be_plain_font->Size() * 1.6f);
	}

	uint32 fColor;
};

// the colors as a submenu, the one that is the current one has a check mark; the messages are copies of
// the template with the color added
static BMenu*
BuildColorMenu(const char* title, const BMessage& message, BHandler* target, bool hasCurrent, uint32 current)
{
	BMenu* menu = new BMenu(title);
	bool known = false;
	for (int c = 0; c < kMarkerColorCount; c++) {
		BMessage* copy = new BMessage(message);
		copy->AddInt32("color", kMarkerColors[c].rgb);
		ColorMenuItem* item = new ColorMenuItem(B_TRANSLATE_NOCOLLECT(kMarkerColors[c].name),
			kMarkerColors[c].rgb, copy);
		item->SetTarget(target);
		if (hasCurrent && current == kMarkerColors[c].rgb) {
			item->SetMarked(true);
			known = true;
		}
		menu->AddItem(item);
	}
	if (hasCurrent && !known) {
		// a color from another program: shown, so that it is clear what the mark has now
		ColorMenuItem* item = new ColorMenuItem(B_TRANSLATE("Current"), current, new BMessage(message));
		item->SetMarked(true);
		item->SetEnabled(false);
		menu->AddItem(item, 0);
		menu->AddItem(new BSeparatorItem(), 1);
	}
	return menu;
}

// more quads than a page can have lines of text
static const int kMaxQuads = 8192;

///////////////////////////////////////////////////////////////////////////
PDFView::PDFView (entry_ref* ref, FileAttributes *fileAttributes,
	const char *name, uint32 flags, const char *ownerPassword,
	const char *userPassword, bool *encrypted)
	: BView(name, flags)
{
	GlobalSettings *settings = gApp->GetSettings();
	SetViewColor(B_TRANSPARENT_COLOR);
	// init member variables
	mDoc = NULL;
	mLoading = false;
	mOk = false;
	mZoom = settings->GetZoom();
	mBitmap = NULL;
	mPage = NULL;
	mActive = NULL;
	mInteractionPage = 0;
	mCanvasLeft = mCanvasTop = 0;
	mCanvasWidth = mCanvasHeight = 100;
	// the active page is there from the start, the slots are set up when a page is shown
	SetActiveRaw(NewSlot());
	mFreeSlots.push_back(mActive);
	mLayout.SetFlow((PageFlow)settings->GetPageFlow());
	mLayout.SetFirstPageAlone(settings->GetTitlePageAlone());
	mCurrentPage = 0;
	mRotation = settings->GetRotation(); // 0.0f;
	mOwnerPassword = mUserPassword = NULL;
	SetPassword(ownerPassword, userPassword);

	mColorSpace = B_RGB32;

	mInvertVerticalScrolling = settings->GetInvertVerticalScrolling();

	mTitle = NULL;
	mLeft = mTop = 0;
	mWidth = 100; mHeight = 100;
	mLink = NULL;
	mTurnState = kTurnNone;
	mTurnBegin = mTurnForward = mTurnForced = false;
	mTurnFreeze = -1;
	mTurnFrom = mTurnTo = mTurnFrame = NULL;
	mTurnFrameView = NULL;
	mTurnStart = 0;
	mTurnRunner = NULL;
	mTurnSound = NULL;
	mTurnSoundMissing = false;
	mNoteTip = 0;
	mNoteHoverSince = 0;
	mNoteReady = false;
	mSelectKeyDown = false;
	mReadOnlyWarned = false;
	mTool = kToolNone;
	mMarkupArmed = false;
	mMarginHover = NULL;
	mKeptLeft = mKeptTop = 0;
	mPendingEdit = NULL;
	mArmedNote = false;
	mArmedType = kMarkupHighlight;
	mArmedColor = 0xffeb3b;
	mToolCursor = new BCursor(B_CURSOR_ID_CROSS_HAIR);
	mAnnotationIndex = -1;
	mAnnotationHandle = kHandleNone;
	static const BCursorID kHandleCursorIds[kHandleCount] = {
		B_CURSOR_ID_MOVE, B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST, B_CURSOR_ID_RESIZE_NORTH_SOUTH,
		B_CURSOR_ID_RESIZE_NORTH_EAST_SOUTH_WEST, B_CURSOR_ID_RESIZE_EAST_WEST,
		B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST, B_CURSOR_ID_RESIZE_NORTH_SOUTH,
		B_CURSOR_ID_RESIZE_NORTH_EAST_SOUTH_WEST, B_CURSOR_ID_RESIZE_EAST_WEST
	};
	for (int h = 0; h < kHandleCount; h++)
		mHandleCursors[h] = new BCursor(kHandleCursorIds[h]);
	mModifierRunner = NULL;
	mLayingOut = false;
	mLayoutThread = -1;
	mLayoutSize = 0;
	mLayoutFromPage = mLayoutToPage = 1;
	mBusyRunner = NULL;
	mFitWidthPending = false;
	mTargetPage = 0;
	mTargetRegion = fz_empty_rect;
	mTargetRunner = NULL;
	mBusyWindow = NULL;
	mNavigationState = kNotInHistory;
	mHistoryOpen = false;

	mViewCursor = NULL;
	mMouseAction = NO_ACTION;
	mMousePosition.Set(0, 0);
	mDragStarted = false;

	mMouseWheelDY = 0;

	mRendering = false;

	mSelected = NOT_SELECTED;
	mTextEndPage = 0;
	mSpansPages = false;
	mSelectionKind = kSelectText;
	mFilledSelection = settings->GetFilledSelection();
	mTextStart = mTextEnd = fz_make_point(0, 0);

	mPrintSettings = NULL;
	mStopFindThread = false;
	mFindPage = 0;
	mFindIndex = -1;
	mFindCaseSensitive = false;
	mFindHighlight = false;
	mRenderedPage = 0;

	if (LoadFile(ref, fileAttributes, ownerPassword, userPassword, true, encrypted)) {
		SetViewCursor(gApp->handCursor, true);
		mOk = true;
	}
}

PDFWindow*
PDFView::GetPDFWindow() {
	return dynamic_cast<PDFWindow*>(Window());
}

///////////////////////////////////////////////////////////////////////////
void PDFView::SetPassword(const char* ownerPassword, const char* userPassword) {
	delete mOwnerPassword; mOwnerPassword = ownerPassword ? new BString(ownerPassword) : NULL;
	delete mUserPassword; mUserPassword = userPassword ? new BString(userPassword) : NULL;
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::EndDoc() {
	mSelected = NOT_SELECTED;
	ClearQuads();
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::UpdatePanelDirectory(BPath* path) {
	BPath directory;
	if (strcmp(path->Path(), gApp->DefaultPDF()->Path()) != 0 &&
		B_OK == path->GetParent(&directory)) {
		// don't set path to default pdf file
		gApp->GetSettings()->SetPanelDirectory(directory.Path());
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::MakeTitleString(BPath* path) {
	delete mTitle;
	mTitle = new BString("Toji: ");

	BString title = mDoc->Metadata(FZ_META_INFO_TITLE);
	if (title.Length() > 0)
		*mTitle << title << " (" << path->Leaf() << ")";
	else
		*mTitle << path->Leaf();
	if (!mDoc->IsWritable())
		*mTitle << " " << B_TRANSLATE("(read-only)");
}

///////////////////////////////////////////////////////////////////////////
bool
PDFView::OpenFile(entry_ref *ref, const char *ownerPassword, const char *userPassword, bool *encrypted) {
	BEntry entry (ref, true);
    if (!entry.Exists()) {
        return false;
    }
	BPath path;
	entry.GetPath (&path);

	// MuPDF knows one password and tries it as user and as owner password
	const char* password = userPassword != NULL && userPassword[0] != '\0' ? userPassword : ownerPassword;

	Document* newDoc = NULL;
	TimingStart();
	Document::OpenResult result = Document::Open(path.Path(), password, &newDoc,
		gApp->GetSettings()->GetTextSize());
	TimingMark("open: document opened");
	*encrypted = result == Document::kNeedsPassword;
	if (result != Document::kOpened)
		return false;

	UpdatePanelDirectory(&path);


	// The pages that are rendered of the previous document are rendered on, bound to that document (which goes away when
	// the last of them has finished); the new document does not wait for them.
	SetSlotsDocument(NULL);
	if (mDoc != NULL)
		mDoc->Release();
	mDoc = newDoc;
	SetSlotsDocument(mDoc);
	MakeTitleString(&path);
	return true;
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::LoadFileSettings(entry_ref* ref, FileAttributes* fileAttributes, float& left, float& top) {
	GlobalSettings *s = gApp->GetSettings();
	bool readOk = fileAttributes->Read(ref, s);
	if (readOk && s->GetRestorePageNumber()) {
		mCurrentPage = fileAttributes->GetPage();
		if (mCurrentPage > mDoc->PageCount()) {
			mCurrentPage = mDoc->PageCount();
		}
		if (mCurrentPage < 1) {
			mCurrentPage = 1;
		}
		// in a book the page number is only right for the text size it was made at: the place is found by its words
		TextAnchor anchor;
		if (mDoc->IsReflowable() && anchor.Unarchive(fileAttributes->GetAnchor())) {
			int page = mDoc->PageOfAnchor(anchor);
			if (page > 0) {
				mCurrentPage = page;
				fileAttributes->SetPage(page);
			}
		}
		mZoom = s->GetZoom();
		mRotation = s->GetRotation();
		fileAttributes->GetLeftTop(left, top);
	} else {
		left = top = 0;
		mCurrentPage = 1;
		fileAttributes->SetPage(mCurrentPage);
		fileAttributes->SetLeftTop(left, top);
	}

	// the direction of reading: what the reader chose for this file, else what the document says about itself
	int reading = fileAttributes->GetReading();
	if (reading < 0)
		reading = mDoc->DeclaredReading();
	mLayout.SetRightToLeft(reading == 1);
	// a webtoon is read by scrolling: its pages are one below the other, as wide as the window (unless the reader has chosen
	// the size for this file). The flow that the reader chose for other documents is not changed.
	bool strips = reading == 2;
	mLayout.SetTopToBottom(strips);
	mLayout.SetFlow(strips ? kFlowContinuous : (PageFlow)s->GetPageFlow());
	mFitWidthPending = strips && !fileAttributes->HasZoom();
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::RestoreWindowFrame(BWindow* w) {
	GlobalSettings* s = gApp->GetSettings();
	if (s->GetRestoreWindowFrame()) {
		// restore window position and size
		w->MoveTo(s->GetWindowPosition());
		float width, height;
		s->GetWindowSize(width, height);
		w->ResizeTo(width, height);
	}
}

///////////////////////////////////////////////////////////////////////////
bool
PDFView::LoadFile(entry_ref *ref, FileAttributes *fileAttributes, const char *ownerPassword, const char *userPassword, bool init, bool *encrypted) {
	BString s(B_TRANSLATE("Toji reading file: "));
	s += ref->name;
	ShowLoadProgressStatusWindow statusWindow(s.String());
	EndDoc();
	WaitForLayout();
	StopBusy();

	SetPassword(ownerPassword, userPassword);

	// We use the application thread to load a file.
	// To keep the window responsive while loading, we unlock the window lock
	// and have to ensure that the window thread does not access data
	// that is being loaded (Draw() just fills the entire view with a background color).
	mLoading = true;
	bool isLocked = Window()->IsLocked();
	if (isLocked) {
		Invalidate();
		Window()->Unlock();
	}
	bool opened = OpenFile(ref, ownerPassword, userPassword, encrypted);
	if (isLocked) Window()->Lock();
	mLoading = false;
	if (!opened) {
		// show previous document
		if (Window()->Lock()) {
			Invalidate();
			Window()->Unlock();
		}
		return false;
	}
#ifdef TOJI_TESTING
	// tests say what to do with legacy attributes (keep or replace) instead of asking
	if (const char* forced = getenv("TOJI_LEGACY"))
		gApp->GetSettings()->SetLegacyAttributes(strcmp(forced, "replace") == 0 ? 2 : 1);
#endif
	BepdfApplication::UpdateFileAttributes(mDoc, ref);
	// the first time a file with attributes of BePDF is seen, the user is asked what to do with them
	if (gApp->GetSettings()->GetLegacyAttributes() == 0 && BepdfApplication::FileHasLegacyAttributes(ref)
		&& Window() != NULL)
		Window()->PostMessage(PDFWindow::LEGACY_ATTRIBUTES_CMD);

	float left, top;
	LoadFileSettings(ref, fileAttributes, left, top);

	RecordHistory(*ref, ownerPassword, userPassword);

	PDFWindow *w = GetPDFWindow();
	if (w && !init && w->Lock()) {
		RestoreWindowFrame(w);
		w->FitToScreen();
		w->NewDoc(mDoc);
		w->SetTitle (mTitle->String());
		mRenderedPage = 0;
		mFindHighlight = false;
		Redraw();
		if (mLayout.IsContinuous())
			ScrollToPage(mCurrentPage, true);
		else
			ScrollTo(left, top);
		w->Unlock();
	}

	if (w != NULL && !init)
		HistoryStart();

	return true;
}

///////////////////////////////////////////////////////////////////////////
PDFView::~PDFView()
{
	CancelTurn();
	delete mTurnSound;
	delete mPendingEdit;
	WaitForLayout();
	StopBusy();
	SetSlotsDocument(NULL);
	for (size_t i = 0; i < mSlots.size(); i++)
		delete mSlots[i];
	for (size_t i = 0; i < mFreeSlots.size(); i++)
		delete mFreeSlots[i];	// they refer to the document
	if (mDoc != NULL)
		mDoc->Release();
	delete mModifierRunner;
	delete mTargetRunner;
	delete mToolCursor;
	for (int h = 0; h < kHandleCount; h++)
		delete mHandleCursors[h];
	delete mTitle;
	delete mOwnerPassword;
	delete mUserPassword;
}

///////////////////////////////////////////////////////////////////////////
void PDFView::MessageReceived(BMessage *msg) {
	BString string;
	switch (msg->what) {
	case B_SIMPLE_DATA: {
			entry_ref ref;
			if (B_OK == msg->FindRef("refs", 0, &ref)) {
				be_app->RefsReceived(msg);
				return;
			}
		}
		break;
	case B_COPY_TARGET:
		SendDataMessage(msg);
		break;
	case B_MOUSE_WHEEL_CHANGED:
		OnMouseWheelChanged(msg);
		break;
	case COPY_LINK_MSG:
		if (B_OK == msg->FindString("link", &string)) {
			CopyText(&string);
		}
		break;
	case COPY_SELECTION_MSG:
		CopySelection();
		break;
	case SELECT_ALL_MSG:
		SelectAll();
		break;
	case TARGET_DONE_MSG:
		ClearTargetRegion();
		break;
	case SHOW_BUSY_MSG:
		// the layout takes a while
		if (mLayingOut) {
			if (mBusyWindow == NULL)
				mBusyWindow = new BusyWindow(Window(), B_TRANSLATE("Laying out the book" B_UTF8_ELLIPSIS));
			mBusyWindow->Appear();
		}
		break;
	case LAYOUT_DONE_MSG:
		if (mLayingOut)
			FinishTextSize();
		break;
	case PAGE_TURN_TICK_MSG:
		TurnTick();
		break;
	case PAGE_TURN_TIMEOUT_MSG:
		// the page that comes was not drawn in time: no turn
		if (mTurnState == kTurnWaiting)
			CancelTurn();
		break;
	case MODIFIERS_POLL_MSG: {
		// a note that the pointer has rested on for the time of the tooltip can be edited by a click
		if (mNoteTip != 0 && !mNoteReady && system_time() - mNoteHoverSince >= kNoteEditDelay) {
			mNoteReady = true;
			BPoint point;
			uint32 buttons;
			GetMouse(&point, &buttons, false);
			if (buttons == 0 && Bounds().Contains(point))
				DisplayLink(point);
		}
		// the cursor shows the selecting mode as soon as the key is down
		bool down = SelectModifierDown();
		if (down != mSelectKeyDown && Window() != NULL && Window()->IsActive()) {
			BPoint point;
			uint32 buttons;
			GetMouse(&point, &buttons, false);
			if (buttons == 0 && Bounds().Contains(point))
				DisplayLink(point);
			mSelectKeyDown = down;
		}
		break;
	}
	case ANNOTATE_MSG: {
		int32 type = kMarkupHighlight, rgb = 0xffeb3b;
		msg->FindInt32("type", &type);
		msg->FindInt32("color", &rgb);
		AnnotateSelection((MarkupType)type, (uint32)rgb);
		break;
	}
	case ARM_MARKUP_MSG: {
		int32 type = kMarkupHighlight, rgb = 0xffeb3b;
		msg->FindInt32("type", &type);
		msg->FindInt32("color", &rgb);
		ArmMarkup((MarkupType)type, (uint32)rgb);
		break;
	}
	case NOTE_MARGIN_MSG:
		// a margin note: for the selected text, or for the next selection of text
		if (HasTextSelection() && mDoc->CanMarkText())
			MarkSelection(kMarkupHighlight, 0xffeb3b, true);
		else if (mDoc->CanMarkText() && mDoc->CanCopy())
			ArmMarkup(kMarkupHighlight, 0xffeb3b, true);
		else
			beep();
		ToolsChanged();
		break;
	case ADD_TOOL_MSG: {
		int32 tool = kToolNone;
		msg->FindInt32("tool", &tool);
		fz_point position;
		bool hasPosition = msg->FindFloat("x", &position.x) == B_OK && msg->FindFloat("y", &position.y) == B_OK;
		SetTool((PlacementTool)tool, hasPosition ? &position : NULL);
		break;
	}
	case CREATE_TEXT_MSG: {
		int32 tool = kToolNote, page = 0;
		fz_point position = fz_make_point(0, 0);
		const char* text = "";
		msg->FindInt32("tool", &tool);
		msg->FindInt32("page", &page);
		msg->FindFloat("x", &position.x);
		msg->FindFloat("y", &position.y);
		msg->FindString("text", &text);
		if (text[0] != '\0' && ConfirmEditable()) {
			bool ok = tool == kToolFreeText ? mDoc->AddFreeText(page, position, text)
				: mDoc->AddNote(page, position, text);
			if (ok)
				AnnotationsChanged(page);
		}
		break;
	}
	case CHANGE_COLOR_MSG: {
		int32 page = 0, index = -1, rgb = 0;
		msg->FindInt32("page", &page);
		msg->FindInt32("index", &index);
		msg->FindInt32("color", &rgb);
		if (ConfirmEditable() && mDoc->SetAnnotationColor(page, index, (uint32)rgb))
			AnnotationsChanged();
		break;
	}
	case COPY_PLACE_LINK_MSG:
		CopyPlaceLink();
		break;
	case CHANGE_STYLE_MSG: {
		// the line width and the fill of a shape
		int32 page = 0, index = -1, rgb = 0;
		float width = 0;
		bool hasFill = false;
		msg->FindInt32("page", &page);
		msg->FindInt32("index", &index);
		msg->FindFloat("width", &width);
		msg->FindBool("hasFill", &hasFill);
		msg->FindInt32("color", &rgb);
		if (ConfirmEditable() && mDoc->SetAnnotationStyle(page, index, width, hasFill, (uint32)rgb))
			AnnotationsChanged();
		break;
	}
	case COPY_ANNOTATION_MSG: {
		int32 page = 0, index = -1;
		msg->FindInt32("page", &page);
		msg->FindInt32("index", &index);
		CopyWebAnnotation(page, index);
		break;
	}
	case DELETE_ANNOTATION_MSG: {
		int32 page = 0, index = -1;
		msg->FindInt32("page", &page);
		msg->FindInt32("index", &index);
		if (ConfirmEditable() && mDoc->DeleteAnnotation(page, index)) {
			mAnnotationIndex = -1;
			AnnotationsChanged();
		}
		break;
	}
	case EDIT_NOTE_MSG: {
		BMessage entered(NOTE_ENTERED_MSG);
		int32 page = 0, index = -1;
		msg->FindInt32("page", &page);
		msg->FindInt32("index", &index);
		entered.AddInt32("page", page);
		entered.AddInt32("index", index);
		const char* text = "";
		msg->FindString("text", &text);
		if (ConfirmEditable())
			new NoteWindow(Window(), BMessenger(this), entered, text);
		break;
	}
	case NOTE_ENTERED_MSG: {
		int32 page = 0, index = -1;
		const char* text = "";
		msg->FindInt32("page", &page);
		msg->FindInt32("index", &index);
		msg->FindString("text", &text);
		if (mDoc->SetAnnotationContents(page, index, text))
			AnnotationsChanged();
		break;
	}
	case OPEN_FILE_MSG:
		if (B_OK == msg->FindString("file", &string)) {
			PDFWindow::Launch(string.String());
		}
		break;
	default:
		BView::MessageReceived(msg);
	}
}

///////////////////////////////////////////////////////////////////////////
bool
PDFView::InPage(BPoint p) {
	return p.x >= 0.0 && p.x < mWidth && p.y >= 0.0 && p.y < mHeight;
}

///////////////////////////////////////////////////////////////////////////
BPoint
PDFView::LimitToPage(BPoint p) {
	if (p.x < 0) p.x = 0.0;
	else if (p.x > mWidth - 1) p.x = mWidth - 1;

	if (p.y < 0) p.y = 0.0;
	else if (p.y > mHeight - 1) p.y = mHeight - 1;
	return p;
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::OnMouseWheelChanged(BMessage *msg) {
	float dy, dx;
	if (msg->FindFloat("be:wheel_delta_y", &dy) == B_OK && dy != 0.0) {
		bool down = dy > 0;
		// as the guidelines say: Command zooms (Control as well, there is no font size to change), Option
		// scrolls a full page; Shift goes to the next or previous page
		int32 keys = modifiers();
		if ((keys & (B_COMMAND_KEY | B_CONTROL_KEY))) {
			Zoom(!down); // zoom in / out
		} else if ((keys & B_OPTION_KEY)) {
			ScrollVertical(down, 1.0);
		} else if ((keys & B_SHIFT_KEY)) {
			if (down)
				NextPage();
			else
				PreviousPage();
		} else {
			ScrollVertical(down, 0.20);
		}
	}
	if (msg->FindFloat("be:wheel_delta_x", &dx) == B_OK && dx != 0.0) {
		bool right = dx > 0;
		ScrollHorizontal(right, 0.20);
	}
}

///////////////////////////////////////////////////////////////////////////
// the active page (see SlotScope) with its frame
void
PDFView::DrawPage(BRect updateRect)
{
	if (mBitmap == NULL) {
#ifdef DEBUG
		fprintf (stderr, "WARNING: PDFView::Draw() NULL bitmap\n");
#endif
		return;
	}

	DrawBitmap(mBitmap, BRect(0, 0, mWidth - 1, mHeight - 1),
		BRect(mLeft, mTop, mLeft + mWidth - 1, mTop + mHeight - 1));
	SetLowColor(ui_color(B_SHADOW_COLOR));
	StrokeRect(BRect(mLeft - 1, mTop - 1, mLeft + mWidth, mTop + mHeight), B_SOLID_LOW);
}

///////////////////////////////////////////////////////////////////////////
// what is around the pages
void
PDFView::DrawBackground(BRect updateRect)
{
	BRegion region(updateRect);
	for (size_t i = 0; i < mSlots.size(); i++) {
		const PageSlot* slot = mSlots[i];
		region.Exclude(BRect(slot->origin.x - 1, slot->origin.y - 1, slot->origin.x + slot->page->GetWidth(),
			slot->origin.y + slot->page->GetHeight()));
	}
	SetHighColor(DesktopColor());
	FillRegion(&region);
}

///////////////////////////////////////////////////////////////////////////
// the region that a deep link has led to
void
PDFView::DrawTargetRegion()
{
	if (!mTargetQuads.empty()) {
		// the words of a quote: marked like with a highlighter, but only for a moment
		SetDrawingMode(B_OP_ALPHA);
		SetHighColor(255, 235, 59, 130);
		for (size_t i = 0; i < mTargetQuads.size(); i++) {
			const fz_quad& q = mTargetQuads[i];
			BPoint polygon[4] = { mPage->PageToDev(q.ul), mPage->PageToDev(q.ur), mPage->PageToDev(q.lr), mPage->PageToDev(q.ll) };
			for (int j = 0; j < 4; j++)
				polygon[j] += BPoint(mLeft, mTop);
			FillPolygon(polygon, 4);
		}
		SetDrawingMode(B_OP_COPY);
		return;
	}
	BPoint a = mPage->PageToDev(fz_make_point(mTargetRegion.x0, mTargetRegion.y0));
	BPoint b = mPage->PageToDev(fz_make_point(mTargetRegion.x1, mTargetRegion.y1));
	BRect rect(fminf(a.x, b.x) + mLeft, fminf(a.y, b.y) + mTop, fmaxf(a.x, b.x) + mLeft, fmaxf(a.y, b.y) + mTop);
	if (rect.Width() < 6 || rect.Height() < 6)
		rect.InsetBy(-6, -6);	// a point is a spot
	SetDrawingMode(B_OP_ALPHA);
	SetHighColor(255, 160, 0, 60);
	FillRect(rect);
	SetDrawingMode(B_OP_COPY);
	SetHighColor(255, 140, 0);
	SetPenSize(2);
	StrokeRect(rect);
	SetPenSize(1);
}


void
PDFView::ClearTargetRegion()
{
	delete mTargetRunner;
	mTargetRunner = NULL;
	mTargetQuads.clear();
	if (mTargetPage != 0) {
		mTargetPage = 0;
		Invalidate();
	}
}


// the mark of a target stays for 3 seconds
void
PDFView::FlashTarget()
{
	delete mTargetRunner;
	BMessage done(TARGET_DONE_MSG);
	mTargetRunner = new BMessageRunner(BMessenger(this), &done, 3000000, 1);
	Invalidate();
}


///////////////////////////////////////////////////////////////////////////
// the places where the search text has been found on this page
void
PDFView::DrawFindHits(BRect updateRect)
{
	if (!mFindHighlight || mPage->mFindQuads.empty())
		return;

	SetHighColor(255, 200, 0, 110);
	SetDrawingMode(B_OP_ALPHA);
	for (size_t i = 0; i < mPage->mFindQuads.size(); i++) {
		const fz_quad& q = mPage->mFindQuads[i];
		BPoint polygon[4] = { mPage->PageToDev(q.ul), mPage->PageToDev(q.ur),
			mPage->PageToDev(q.lr), mPage->PageToDev(q.ll) };
		for (int j = 0; j < 4; j++)
			polygon[j] += BPoint(mLeft, mTop);
		FillPolygon(polygon, 4);
	}
	SetDrawingMode(B_OP_COPY);
}


static int
CollectQuads(fz_context*, void* data, int numQuads, fz_quad* quads, int, int)
{
	std::vector<fz_quad>* all = (std::vector<fz_quad>*)data;
	for (int i = 0; i < numQuads; i++)
		all->push_back(quads[i]);
	return 0;
}


// Finds all hits of the last search on a page that is shown, once it has been rendered (the text is not there before).
void
PDFView::UpdateFindQuads(PageSlot* slot)
{
	std::vector<fz_quad> quads;
	fz_stext_page* text = mFindHighlight ? slot->page->Text() : NULL;
	if (text != NULL) {
		DocumentLocker locker(mDoc);
		fz_context* context = mDoc->Context();
		fz_try(context) {
			fz_match_stext_page_cb(context, text, mFindNeedle.String(), CollectQuads, &quads,
				mFindCaseSensitive ? FZ_SEARCH_EXACT : FZ_SEARCH_IGNORE_CASE);
		}
		fz_catch(context) {
			quads.clear();
		}
	}
	slot->page->mFindQuads.swap(quads);
	Invalidate();
}


void
PDFView::UpdateFindQuadsOfAll()
{
	for (size_t i = 0; i < mSlots.size(); i++)
		UpdateFindQuads(mSlots[i]);
}


void
PDFView::ClearFindHighlights()
{
	mFindHighlight = false;
	for (size_t i = 0; i < mSlots.size(); i++)
		mSlots[i]->page->mFindQuads.clear();
	Invalidate();
}


///////////////////////////////////////////////////////////////////////////
void
PDFView::DrawSelection(BRect updateRect)
{
	if (mSelected == NOT_SELECTED)
		return;

	rgb_color fill_color = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
	fill_color.alpha = 70;
	SetHighColor(fill_color); // fill color for selection
	SetPenSize(1.0);

	if (mSelectionKind == kSelectText) {
		// the text between the two points, line by line
		SetDrawingMode(B_OP_ALPHA);
		const std::vector<fz_quad>* quads = QuadsOnPage(mActive->number);
		if (quads == NULL) {
			SetDrawingMode(B_OP_COPY);
			return;
		}
		for (size_t i = 0; i < quads->size(); i++) {
			const fz_quad& q = (*quads)[i];
			BPoint polygon[4] = { mPage->PageToDev(q.ul), mPage->PageToDev(q.ur),
				mPage->PageToDev(q.lr), mPage->PageToDev(q.ll) };
			for (int j = 0; j < 4; j++)
				polygon[j] += BPoint(mLeft, mTop);
			FillPolygon(polygon, 4);
		}
		SetDrawingMode(B_OP_COPY);
		return;
	}

	BRect selection(mSelection);
	selection.OffsetBy(mLeft, mTop);

	switch (mSelected) {
		case DO_SELECTION:
			StrokeRect(selection);
			break;
		case SELECTED:
			SetDrawingMode(B_OP_ALPHA);
			if (mFilledSelection) {
				FillRect(selection);
			} else {
				StrokeRect(selection);
			}
			SetDrawingMode(B_OP_COPY);
			break;
		default:
			break;
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::Draw(BRect updateRect)
{
	if (mLoading) {
		SetLowColor(DesktopColor());
		FillRect(updateRect, B_SOLID_LOW);
	} else if (mTurnState != kTurnNone && mTurnFrom != NULL) {
		DrawTurn();
	} else {
		DrawBackground(updateRect);
		BRect rect(Bounds());
		if (GetPDFWindow()) {
			GetPDFWindow()->GetFileAttributes()->SetLeftTop(rect.left, rect.top);
		}
		for (size_t i = 0; i < mSlots.size(); i++) {
			PageSlot* slot = mSlots[i];
			BRect area(slot->origin.x - 1, slot->origin.y - 1, slot->origin.x + slot->page->GetWidth(),
				slot->origin.y + slot->page->GetHeight());
			if (!area.Intersects(updateRect))
				continue;
			// the members stand for this page while it is drawn
			SlotScope scope(this, slot);
			DrawPage(updateRect);
			DrawFindHits(updateRect);
			DrawMarginNotes(updateRect);
			if (slot->number == mTargetPage)
				DrawTargetRegion();
			if (slot->number == mInteractionPage) {
				DrawSelection(updateRect);
				DrawToolPreview();
				DrawAnnotationSelection();
			} else if (mSelected != NOT_SELECTED && mSelectionKind == kSelectText && mSpansPages)
				DrawSelection(updateRect);
		}
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::ScrollTo (BPoint point) {
	// (what the resizing of the window makes of the scroll bars is not a place that was scrolled to: Resize() puts the view back
	// and brings the pages, until then the pages and the current page are left alone, which would make them flicker)
	BMessage* now = Window() != NULL ? Window()->CurrentMessage() : NULL;
	bool resizing = now != NULL && now->what == B_VIEW_RESIZED;
	// the view stops at the limits of the document (a request beyond them, from the wheel or a fast drag, would be clamped by the
	// scroll bars and asked for again: a flicker)
	BRect view(Bounds());
	point.x = max_c(0.0f, min_c(point.x, mCanvasWidth - view.Width()));
	point.y = max_c(0.0f, min_c(point.y, mCanvasHeight - view.Height()));
	if (!resizing) {
		mKeptLeft = point.x;
		mKeptTop = point.y;
		if (point == view.LeftTop())
			return;		// (at a limit, a wheel or a drag that asks for more changes nothing: no work, no redraw)
	}
	BView::ScrollTo(point);
	if (resizing)
		return;
	UpdateVisibleSlots();	// pages that come into view
	BPoint mouse; uint32 buttons;
	GetMouse(&mouse, &buttons);
	DisplayLink(mouse);
}

void
PDFView::ScrollTo(float x, float y) {
	BRect bounds(Bounds());
	float xMax = mCanvasWidth - bounds.Width();
	float yMax = mCanvasHeight - bounds.Height();

	if ((x < 0) || (mCanvasLeft > 0))
		x = 0;
	else if ((xMax > 0) && (x > xMax))
		x = xMax;

	if ((y < 0) || (mCanvasTop > 0))
		y = 0;
	else if ((yMax > 0) && (y > yMax))
		y = yMax;

	BView::ScrollTo(x, y);
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::FrameResized (float width, float height)
{
	CancelTurn();
	Resize();
}


///////////////////////////////////////////////////////////////////////////
void
PDFView::AttachedToWindow ()
{
	Window()->SetTitle (mTitle->String());
	SetViewCursor(gApp->handCursor);
	for (size_t i = 0; i < mSlots.size(); i++)
		mSlots[i]->renderer->SetListener(Window(), this);
	for (size_t i = 0; i < mFreeSlots.size(); i++)
		mFreeSlots[i]->renderer->SetListener(Window(), this);

	// there is no message when a key like Option is pressed while the mouse rests, so look from time to time
	if (mModifierRunner == NULL) {
		BMessage poll(MODIFIERS_POLL_MSG);
		mModifierRunner = new BMessageRunner(BMessenger(this), &poll, 100000);
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::ScrollVertical (bool down, float by) {
	BRect rect(Bounds ());
	float scrollBy = (by > 0) ? rect.Height() * by : -by;
	bool continuous = mLayout.IsContinuous();
	if (down) {
		if (rect.bottom < mCanvasHeight-1) {
			ScrollBy (0, scrollBy);
		} else if (!continuous) {
			// the bottom of the page (the spread) has been reached, go to the next one
			int next = mLayout.NextSpreadPage(mCurrentPage);
			if (next != mCurrentPage)
				MoveToPage(next, true);
		}
	} else { // up
		if (rect.top != 0) {
			ScrollBy (0, -scrollBy);
		} else if (!continuous) {
			int previous = mLayout.PreviousSpreadPage(mCurrentPage);
			if (previous != mCurrentPage)
				MoveToPage(previous, false);
		}
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::ScrollHorizontal (bool right, float by) {
	BRect rect(Bounds());
	float scrollBy = (by > 0) ? rect.Width() * by : -by;
	if (right) {
		if (rect.right < mCanvasWidth - 1) {
			ScrollBy (scrollBy, 0);
		}
	} else {
		if (rect.left != 0) {
			ScrollBy (-scrollBy, 0);
		}
	}
}
///////////////////////////////////////////////////////////////////////////
void
PDFView::KeyDown (const char * bytes, int32 numBytes)
{
	if (mLayingOut)
		return;
	if (mTurnState != kTurnNone && *bytes == B_ESCAPE) {
		CancelTurn();
		return;
	}
	switch (*bytes) {
	case B_ESCAPE:
		if (mMarkupArmed)
			DisarmMarkup();
		else if (mTool != kToolNone)
			CancelTool();
		else if (mAnnotationIndex >= 0)
			SelectAnnotation(-1);
		else if (mFindHighlight)
			ClearFindHighlights();		// the places where the search found something
		else
			BView::KeyDown(bytes, numBytes);
		break;
	case B_DELETE:
		if (mAnnotationIndex >= 0)
			DeleteSelectedAnnotation();
		else
			BView::KeyDown(bytes, numBytes);
		break;
	case B_PAGE_UP:
		PreviousPage();
		break;
	case B_SPACE:
	case B_ENTER:
	case B_BACKSPACE:
		ScrollVertical (*bytes != B_BACKSPACE, 0.95);
		break;
	case B_DOWN_ARROW:
	case B_UP_ARROW:
		ScrollVertical (*bytes == B_DOWN_ARROW, -20);
		break;
	case B_LEFT_ARROW:
	case B_RIGHT_ARROW:
		ScrollHorizontal(*bytes == B_RIGHT_ARROW, -20);
		break;
	case B_PAGE_DOWN:
		NextPage();
		break;
	case B_HOME:
		MoveToPage (1);
		break;
	case B_END:
		MoveToPage (GetNumPages ());
		break;
	default:
		BView::KeyDown (bytes, numBytes);
		break;
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::SetAction(mouse_action action) {
	mMouseAction = action;
}

///////////////////////////////////////////////////////////////////////////
void PDFView::SetViewCursor(BCursor *cursor, bool sync) {
	if (Window()->Lock()) {
		mViewCursor = cursor;
		BView::SetViewCursor(cursor, sync);
		Window()->Unlock();
	}
}

///////////////////////////////////////////////////////////////////////////
BPoint
PDFView::CorrectMousePos(const BPoint point) {
	BPoint p(point);
	p.x -= mLeft;
	p.y -= mTop;
	return p;
}

///////////////////////////////////////////////////////////////////////////
// Option (and Alt) with the primary button selects text, like in other readers: no mode to switch.
static bool
SelectModifierDown()
{
	return (modifiers() & (B_OPTION_KEY | B_COMMAND_KEY)) != 0;
}

uint32
PDFView::GetButtons() {
	BPoint point;
	uint32 buttons;
	GetMouse(&point, &buttons, false);
	if (buttons == B_PRIMARY_MOUSE_BUTTON && !SelectModifierDown()) {
		if ((modifiers() & B_CONTROL_KEY)) {
			buttons = B_SECONDARY_MOUSE_BUTTON; // simulate secondary button
		} else if ((modifiers() & B_SHIFT_KEY)) {
			buttons = B_TERTIARY_MOUSE_BUTTON; // simulate tertiary button
		}
	}
	return buttons;
}

///////////////////////////////////////////////////////////////////////////
// Starts a selection at the position: of the text that follows the flow of the text, or a rectangle.
void
PDFView::BeginSelection(BPoint point, bool rectangle) {
	if (mSelected != NOT_SELECTED) {
		BRect old = SelectionBounds();
		mSelected = NOT_SELECTED;
		ClearQuads();
		if (old.IsValid())
			Invalidate(old.InsetByCopy(-2, -2).OffsetByCopy(mLeft, mTop));
	}

	SetAction(SELECT_ACTION);
	mSelected = DO_SELECTION;
	mMousePosition = ConvertToScreen(point);
	point = LimitToPage(CorrectMousePos(point));
	SetViewCursor(gApp->textSelectionCursor);
	mSelectionKind = rectangle ? kSelectArea : kSelectText;
	mSelectionStart = point;
	mSelection.SetLeftTop(point);
	mSelection.SetRightBottom(point);
	if (mSelectionKind == kSelectText)
		StartTextSelection(point);
	SetMouseEventMask(B_POINTER_EVENTS);
}

// Opens the note or the text on the page at a point of the view for editing, if there is one: when the mouse button is
// released if it is for a double click (a window that opens while the button is down gets the rest of the click), or at once.
// With "marks" a mark of text that carries a note counts as well.
bool
PDFView::EditNoteAt(BPoint point, bool marks, bool whenReleased)
{
	const DocAnnotation* note = OnAnnotation(point);
	if (note == NULL)
		return false;
	bool wanted = note->kind == kAnnotNote || note->kind == kAnnotText
		|| (marks && note->kind == kAnnotMarkup && note->contents.Length() > 0 && mNoteReady
			&& mNoteTip == note->index + 1);
	if (!wanted)
		return false;
	BMessage edit(EDIT_NOTE_MSG);
	edit.AddInt32("page", ActivePage());
	edit.AddInt32("index", note->index);
	edit.AddString("text", note->contents.String());
	if (whenReleased) {
		delete mPendingEdit;
		mPendingEdit = new BMessage(edit);
	} else if (Window() != NULL)
		Window()->PostMessage(&edit, this);
	return true;
}


///////////////////////////////////////////////////////////////////////////
void
PDFView::MouseDown (BPoint point) {
	BPoint screen;
	CancelTurn();

	// (the document is being laid out, nothing can be done with it)
	if (mLayingOut)
		return;

	MakeFocus(true);
	uint32 buttons = GetButtons();
	screen = ConvertToScreen(point);

	// the page that is clicked is the one that the click works with
	if (PageSlot* clicked = SlotAt(point))
		ActivateSlot(clicked);

	int32 clicks = 1;
	BMessage* current = Window()->CurrentMessage();
	if (current != NULL)
		current->FindInt32("clicks", &clicks);

	if (mTool != kToolNone) {
		if (buttons == B_PRIMARY_MOUSE_BUTTON) {
			BeginTool(point);
			return;
		}
		CancelTool();	// another button gets its usual meaning
	}

	// a click on the note in the margin opens it for editing
	if (buttons == B_PRIMARY_MOUSE_BUTTON && !SelectingText()) {
		PageSlot* noteSlot = NULL;
		const DocAnnotation* margin = MarginNoteAtView(point, &noteSlot);
		if (margin != NULL && mNoteReady && mNoteTip == margin->index + 1) {
			ActivateSlot(noteSlot);
			BMessage edit(EDIT_NOTE_MSG);
			edit.AddInt32("page", ActivePage());
			edit.AddInt32("index", margin->index);
			edit.AddString("text", margin->contents);
			if (Window() != NULL)
				Window()->PostMessage(&edit, this);
			return;
		}
	}

	// a double click on a note or a text opens it for editing
	if (buttons == B_PRIMARY_MOUSE_BUTTON && clicks >= 2 && !SelectingText() && EditNoteAt(point, false, true))
		return;
	// a click on a mark that has a note opens the note, like the note in the margin
	if (buttons == B_PRIMARY_MOUSE_BUTTON && clicks == 1 && !SelectingText() && EditNoteAt(point, true, false))
		return;

	// a click on an annotation that can be moved selects it, and a drag from there moves it; elsewhere the click
	// has its usual meaning
	if (buttons == B_PRIMARY_MOUSE_BUTTON && !SelectingText() && BeginAnnotationDrag(point))
		return;
	if (buttons == B_SECONDARY_MOUSE_BUTTON) {
		const DocAnnotation* clicked = OnAnnotation(point);
		if (clicked != NULL && clicked->kind != kAnnotMarkup && clicked->kind != kAnnotOther)
			SelectAnnotation(clicked->index);
	}

	switch (buttons) {
		case B_PRIMARY_MOUSE_BUTTON:
			// Option: select text, with Shift a rectangle (which also copies the picture of it)
			if (SelectingText()) {
				// (the marker takes a word or a line at a double or triple click)
				if (mMarkupArmed && clicks >= 2 && mDoc->CanCopy()
					&& SelectTextAt(point, clicks == 2 ? FZ_SELECT_WORDS : FZ_SELECT_LINES)) {
					SelectionChanged();
					ApplyArmedMarkup();
					break;
				}
				if (mDoc->CanCopy())
					BeginSelection(point, !mMarkupArmed && (modifiers() & B_SHIFT_KEY) != 0);
				break;
			}
			if ((mSelected == SELECTED) && InSelection(point)) {
				SendDragMessage(B_MIME_DATA); // start text drag and drop
				break;
			}
			// double click selects a word, triple click a line
			if (clicks >= 2 && mDoc->CanCopy()
				&& SelectTextAt(point, clicks == 2 ? FZ_SELECT_WORDS : FZ_SELECT_LINES)) {
				CopySelection();
				SelectionChanged();
				break;
			}
			// a click outside the selected text lets go of the selection, and of the marks of the search
			if (mSelected == SELECTED)
				SelectNone();
			if (mFindHighlight)
				ClearFindHighlights();
			// follow link or move view
			SetAction(MOVE_ACTION);
			if (!HandleLink(point)) {
				mDragStarted = true;
				SetMouseEventMask(B_POINTER_EVENTS);
		  		SetViewCursor(gApp->grabCursor);
				mMousePosition = ConvertToScreen(point);
			} else {
				SetAction(NO_ACTION);
			}
			break;
		case B_SECONDARY_MOUSE_BUTTON:
			if ((mSelected == SELECTED) && InSelection(point)) {
				// a click opens the menu (see MouseUp), moving the mouse starts to drag the selection
				mSecondaryStart = screen;
				SetAction(SECONDARY_ACTION);
				SetMouseEventMask(B_POINTER_EVENTS);
				return;
			}
			ShowPopUpMenu(screen, OnLink(point), OnAnnotation(point));
			return;
		case B_TERTIARY_MOUSE_BUTTON: // zoom to selection
			if (mSelected != NOT_SELECTED) {
				BRect old = SelectionBounds();
				mSelected = NOT_SELECTED;
				ClearQuads();
				if (old.IsValid())
					Invalidate(old.InsetByCopy(-2, -2).OffsetByCopy(mLeft, mTop));
			}

			SetAction(ZOOM_ACTION);
			mSelected = DO_SELECTION;
			mMousePosition = screen;
			point = CorrectMousePos(point);
		  	SetViewCursor(gApp->zoomCursor);
		  	mSelectionKind = kSelectArea;
			mSelectionStart = point;
			mSelection.SetLeftTop(point);
			mSelection.SetRightBottom(point);
			SetMouseEventMask(B_POINTER_EVENTS);
			break;
	}
}


void
PDFView::ScrollIfOutside(BPoint point) {
	float x, y, r_min, r_max;
	BRect bounds(Bounds());

	BScrollBar *scroll = ScrollBar(B_VERTICAL);

	scroll->GetRange(&r_min, &r_max);

	if (point.x < bounds.left) { // scroll left
		x = point.x;
	} else if (point.x > bounds.right) { // scroll right
		x = point.x - bounds.Width();
	} else {
		x = bounds.left;
	}
	x = min_c(r_max, max_c(x, r_min));

	scroll = ScrollBar(B_VERTICAL);
	scroll->GetRange(&r_min, &r_max);
	if (point.y < bounds.top) { // scroll up
		y = point.y;
	} else if (point.y > bounds.bottom) { // scroll down
		y = point.y - bounds.Height();
	} else {
		y = bounds.top;
	}
	y = min_c(r_max, max_c(y, r_min));
	if ((x != bounds.left) || (y != bounds.top)) {
		ScrollTo(x, y);
	}
}

///////////////////////////////////////////////////////////////////////////
void PDFView::SkipMouseMoveMsgs() {
	BMessage *mouseMovedMsg;
	while ((mouseMovedMsg = Looper()->MessageQueue()->FindMessage(B_MOUSE_MOVED, 0)))
	{
		Looper()->MessageQueue()->RemoveMessage(mouseMovedMsg);
		delete mouseMovedMsg;
	}
}

void
PDFView::InitViewCursor(uint32 transit) {
	// FIXME: Where is the best place to set the initial Cursor of a view?
	if ((transit == B_ENTERED_VIEW) && (mViewCursor != NULL)) {
		if (Window()->Lock()) {
			BView::SetViewCursor(mViewCursor);
			Window()->Unlock();
			mViewCursor = NULL;
		}
	}
}

void
PDFView::MouseMoved (BPoint point, uint32 transit, const BMessage *msg) {
	#define UPDATE_INTERVAL 4
	int updateCounter = UPDATE_INTERVAL;

	InitViewCursor(transit);

	switch (mMouseAction) {
		case NO_ACTION:
			if (!mDragStarted) {
				DisplayLink(point);
				const DocAnnotation* hover = transit == B_EXITED_VIEW ? NULL : MarginNoteAtView(point, NULL);
				if (hover != mMarginHover) {
					mMarginHover = hover;
					Invalidate();
				}
			}
			break;
		case MOVE_ACTION: // move view
		{
			SkipMouseMoveMsgs();

			BPoint mousePosition = point;
			uint32 buttons = GetButtons();
			BPoint offset;
			float x, y, r_min, r_max;
			BScrollBar *scroll;

			point = ConvertToScreen(mousePosition);
			offset = point - mMousePosition;
			if (mInvertVerticalScrolling) {
				offset.y = mMousePosition.y - point.y;
			}
			mMousePosition = point;

			scroll = ScrollBar(B_HORIZONTAL);
			scroll->GetRange(&r_min, &r_max);
			x = min_c(r_max, max_c(scroll->Value() - offset.x, r_min));

			scroll = ScrollBar(B_VERTICAL);
			scroll->GetRange(&r_min, &r_max);
			y = min_c(r_max, max_c(scroll->Value() - offset.y, r_min));

			ScrollTo(x, y);

			if ((buttons & B_PRIMARY_MOUSE_BUTTON) == 0) {
				MouseUp(mousePosition);
			}
			break;
		}
		case ANNOT_ACTION:
		{
			SkipMouseMoveMsgs();
			BRect old = mAnnotationPreview;
			BPoint p = CorrectMousePos(point);
			float dx = p.x - mAnnotationDragStart.x, dy = p.y - mAnnotationDragStart.y;
			BRect r = mAnnotationOriginal;
			const float kMinimum = 8;
			switch (mAnnotationHandle) {
				case kHandleMove:
					r.OffsetBy(dx, dy);
					// not out of the page
					if (r.left < 0)
						r.OffsetBy(-r.left, 0);
					if (r.top < 0)
						r.OffsetBy(0, -r.top);
					if (r.right > mWidth)
						r.OffsetBy(mWidth - r.right, 0);
					if (r.bottom > mHeight)
						r.OffsetBy(0, mHeight - r.bottom);
					break;
				case kHandleNorthWest:
					r.left = min_c(r.left + dx, r.right - kMinimum);
					r.top = min_c(r.top + dy, r.bottom - kMinimum);
					break;
				case kHandleNorth:
					r.top = min_c(r.top + dy, r.bottom - kMinimum);
					break;
				case kHandleNorthEast:
					r.right = max_c(r.right + dx, r.left + kMinimum);
					r.top = min_c(r.top + dy, r.bottom - kMinimum);
					break;
				case kHandleEast:
					r.right = max_c(r.right + dx, r.left + kMinimum);
					break;
				case kHandleSouthEast:
					r.right = max_c(r.right + dx, r.left + kMinimum);
					r.bottom = max_c(r.bottom + dy, r.top + kMinimum);
					break;
				case kHandleSouth:
					r.bottom = max_c(r.bottom + dy, r.top + kMinimum);
					break;
				case kHandleSouthWest:
					r.left = min_c(r.left + dx, r.right - kMinimum);
					r.bottom = max_c(r.bottom + dy, r.top + kMinimum);
					break;
				case kHandleWest:
					r.left = min_c(r.left + dx, r.right - kMinimum);
					break;
			}
			mAnnotationPreview = r;
			Invalidate((old | r).InsetByCopy(-8, -8).OffsetByCopy(mLeft, mTop));
			if ((GetButtons() & B_PRIMARY_MOUSE_BUTTON) == 0)
				MouseUp(point);
			break;
		}
		case TOOL_ACTION:
		{
			SkipMouseMoveMsgs();
			BRect old = ToolBounds();
			BPoint p = LimitToPage(CorrectMousePos(point));
			mToolEnd = p;
			if (mTool == kToolInk) {
				BPoint last = mToolPoints.back();
				if (fabsf(p.x - last.x) >= 2 || fabsf(p.y - last.y) >= 2)
					mToolPoints.push_back(p);
			}
			Invalidate((old | ToolBounds()).InsetByCopy(-4, -4).OffsetByCopy(mLeft, mTop));
			if ((GetButtons() & B_PRIMARY_MOUSE_BUTTON) == 0)
				MouseUp(point);
			break;
		}
		case SECONDARY_ACTION:
		{
			uint32 buttons = GetButtons();
			BPoint offset = ConvertToScreen(point) - mSecondaryStart;
			if ((buttons & B_SECONDARY_MOUSE_BUTTON) == 0) {
				MouseUp(point);
			} else if (fabsf(offset.x) > 4 || fabsf(offset.y) > 4) {
				SetAction(NO_ACTION);
				SendDragMessage(B_SIMPLE_DATA); // start negotiated drag and drop
			}
			break;
		}
		case SELECT_ACTION: // text selection
		case ZOOM_ACTION: // zoom to selection
		 	while(true) {
				SkipMouseMoveMsgs();

				uint32 buttons;

				ScrollIfOutside(point);

				switch (mMouseAction) {
					case SELECT_ACTION:
					case ZOOM_ACTION:
						ResizeSelection(point);
						break;
					default:;
				}

				GetMouse(&point, &buttons, false);
				if (buttons == 0) {
					MouseUp(point);
					return;
				}
				if (updateCounter == UPDATE_INTERVAL) {
					Window()->UpdateIfNeeded();
					updateCounter = 0;
				} else {
					updateCounter ++;
				}
				snooze(10000);
			}
			break;
		default:;
	}
}

void
PDFView::ResizeSelection(BPoint point) {
	const BPoint viewPoint = point;
	point = CorrectMousePos(point);
	if (mMouseAction == SELECT_ACTION && mSelectionKind == kSelectText) {
		ExtendTextSelectionTo(viewPoint);
		return;
	}

	BRect rect(mSelection);
	if (mMouseAction == SELECT_ACTION) point = LimitToPage(point);
	if (point.x < mSelectionStart.x) {
		mSelection.left = point.x; mSelection.right = mSelectionStart.x;
	} else {
		mSelection.left = mSelectionStart.x; mSelection.right = point.x;
	}

	if (point.y < mSelectionStart.y) {
		mSelection.top = point.y; mSelection.bottom = mSelectionStart.y;
	} else {
		mSelection.top = mSelectionStart.y; mSelection.bottom = point.y;
	}

	if (rect != mSelection) {
		rect = rect | mSelection;
		rect.OffsetBy(mLeft, mTop);
		Invalidate(rect);
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::MouseUp (BPoint point) {
	if (mPendingEdit != NULL) {
		// the note of a double click opens now
		BMessage edit(*mPendingEdit);
		delete mPendingEdit;
		mPendingEdit = NULL;
		if (Window() != NULL)
			Window()->PostMessage(&edit, this);
		return;
	}
	if (mMouseAction == ANNOT_ACTION) {
		SetAction(NO_ACTION);
		FinishAnnotationDrag();
		return;
	}
	if (mMouseAction == TOOL_ACTION) {
		SetAction(NO_ACTION);
		FinishTool(point);
		return;
	}
	if (mMouseAction == SECONDARY_ACTION) {
		SetAction(NO_ACTION);
		ShowPopUpMenu(ConvertToScreen(point), OnLink(point), OnAnnotation(point));
		return;
	}
	if (mMouseAction == SELECT_ACTION) { // copy selection
		if (mSelectionKind == kSelectText) {
			BRect bounds = SelectionBounds();
			if (!mQuads.empty() || TextEndPage() != mInteractionPage) {
				mSelected = SELECTED;
				if (mMarkupArmed)
					ApplyArmedMarkup();
				else
					CopySelection();
			} else {
				mSelected = NOT_SELECTED;
			}
			if (bounds.IsValid())
				Invalidate(bounds.InsetByCopy(-2, -2).OffsetByCopy(mLeft, mTop));
		} else if (mSelection.Width() * mSelection.Height() * mSelection .Height() > 200) {
			mSelected = SELECTED;
			Invalidate(mSelection.OffsetByCopy(mLeft, mTop));
			CopySelection();
		} else {
			mSelected = NOT_SELECTED;
			Invalidate(mSelection.OffsetByCopy(mLeft, mTop));
		}
	} else if (mMouseAction == ZOOM_ACTION) { // zoom to selection
		mSelected = NOT_SELECTED;
		Invalidate(mSelection.OffsetByCopy(mLeft, mTop));

		if (mSelection.Width() * mSelection .Height() > 200) {
			float a = mSelection.Width() + 1, b = mSelection.Height() + 1;
			BRect bounds(Bounds());
			float n = bounds.Width() + 1, m = bounds.Height() + 1;

			int32 zoomDPI = GetZoomDPI(), newZoomDPI;
			if (a / b > n / m) {
				newZoomDPI = (int32)(zoomDPI * n / a);
			} else {
				newZoomDPI = (int32)(zoomDPI * m / b);
			}

			if (newZoomDPI > ZOOM_DPI_MAX) newZoomDPI = ZOOM_DPI_MAX;
			float x = mSelection.left * newZoomDPI / zoomDPI,
				y = mSelection.top * newZoomDPI / zoomDPI;

			int i;
			for (i = MIN_ZOOM; i <= MAX_ZOOM; i++) {
				if (newZoomDPI == kZoomDPI[i]) {
					newZoomDPI = i; break;
				}
			}

			if (i > MAX_ZOOM)
				newZoomDPI = -newZoomDPI;

			if (mZoom != newZoomDPI) {
				PDFWindow* w = GetPDFWindow();
				if (w) w->SetZoom(newZoomDPI);
				SetZoom(newZoomDPI);
			}

			ScrollTo(x, y);
		}
	}

	SelectionChanged();

	if ((mMouseAction != NO_ACTION) || mDragStarted) {
		mDragStarted = false;
		DisplayLink(point);
		mMouseAction = NO_ACTION;
	}
}

///////////////////////////////////////////////////////////////////////////
const DocAnnotation*
PDFView::OnAnnotation(BPoint point) {
	if (mRendering || (mDoc == NULL) || (mDoc->PageCount() == 0)) return NULL;

	point = CorrectMousePos(point);
	return mPage->FindAnnotation(mPage->DevToPage(point), 5.0f / mPage->Scale());
}

const DocLink*
PDFView::OnLink(BPoint point) {
	if (mRendering || (mDoc == NULL) || (mDoc->PageCount() == 0)) return NULL;

	point = CorrectMousePos(point);
	return mPage->FindLink(mPage->DevToPage(point));
}

// the path of the document a link points to, if it is a link to a document that can be opened
bool
PDFView::IsLinkToDocument(const DocLink* link, BString* path) {
	BString file(link->uri);
	if (file.StartsWith("file://"))
		file.Remove(0, 7);
	else if (file.Length() == 0 || mDoc->IsExternalLink(file.String()))
		return false;	// has a scheme like "http:"

	int32 fragment = file.FindFirst('#');
	if (fragment >= 0)
		file.Truncate(fragment);
	if (file.Length() == 0 || !file.IEndsWith(".pdf"))
		return false;

	if (file[0] != '/') {
		BPath directory;
		if (BPath(mDoc->Path()).GetParent(&directory) != B_OK)
			return false;
		directory.Append(file.String());
		file = directory.Path();
	}
	*path = file;
	return true;
}

///////////////////////////////////////////////////////////////////////////
bool
PDFView::HandleLink(BPoint point) {
	const DocLink* link = OnLink(point);
	if (link == NULL)
		return false;

	BString pdfFile;
	if (IsLinkToDocument(link, &pdfFile)) {
		RecordHistory();
		if ((modifiers() & B_COMMAND_KEY)) {
			PDFWindow::Launch(pdfFile.String());
		} else {
			PDFWindow::OpenInWindow(pdfFile.String());
		}
		return true;
	}

	int page;
	float x, y;
	if (mDoc->ResolveLink(link->uri.String(), &page, &x, &y)) {
		GotoPosition(page, x, y);
		return true;
	}

	// anything else with a scheme: let the system decide
	if (mDoc->IsExternalLink(link->uri.String()) && GetPDFWindow()) {
		GetPDFWindow()->LaunchHTMLBrowser(link->uri.String());
		return true;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::GotoPosition(int page, float x, float y) {
	int target = page > 0 ? page : 1;
	if (target != mCurrentPage)
		MoveToPage(target);

	if (isnan(x) && isnan(y))
		return;

	// the position is on the page that was gone to: its slot (the active page may be another one, in a continuous flow) and
	// the place of the slot in the view
	PageSlot* slot = SlotForPage(target);
	if (slot == NULL)
		return;
	BPoint dev = slot->page->PageToDev(fz_make_point(isnan(x) ? 0 : x, isnan(y) ? 0 : y));
	BRect bounds(Bounds());
	ScrollTo(isnan(x) ? bounds.left : slot->origin.x + dev.x, isnan(y) ? bounds.top : slot->origin.y + dev.y);
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::ShowPopUpMenu(BPoint point, const DocLink* link, const DocAnnotation* annotation) {
	BPopUpMenu* menu = new BPopUpMenu("PopUpMenu");
	menu->SetAsyncAutoDestruct(true);

	BMessage* msg;
	BMenuItem* i;
	BString s;

	bool canCopy = mDoc->CanCopy();
	i = new BMenuItem(B_TRANSLATE("Copy"), new BMessage(COPY_SELECTION_MSG));
	i->SetTarget(this);
	i->SetEnabled(canCopy && mSelected == SELECTED);
	menu->AddItem(i);
	i = new BMenuItem(B_TRANSLATE("Select all"), new BMessage(SELECT_ALL_MSG));
	i->SetTarget(this);
	i->SetEnabled(canCopy);
	menu->AddItem(i);
	i = new BMenuItem(B_TRANSLATE("Copy link to this place"), new BMessage(COPY_PLACE_LINK_MSG));
	i->SetTarget(this);
	menu->AddItem(i);

	if (mDoc->CanEditAnnotations()) {
		menu->AddSeparatorItem();

		if (HasTextSelection() && mDoc->CanMarkText()) {
			BMessage highlight(ANNOTATE_MSG);
			highlight.AddInt32("type", kMarkupHighlight);
			menu->AddItem(BuildColorMenu(B_TRANSLATE("Highlight"), highlight, this, false, 0));

			msg = new BMessage(ANNOTATE_MSG);
			msg->AddInt32("type", kMarkupUnderline);
			msg->AddInt32("color", kMarkerColors[5].rgb);
			i = new BMenuItem(B_TRANSLATE("Underline"), msg);
			i->SetTarget(this);
			menu->AddItem(i);

			msg = new BMessage(ANNOTATE_MSG);
			msg->AddInt32("type", kMarkupStrikeOut);
			msg->AddInt32("color", kMarkerColors[5].rgb);
			i = new BMenuItem(B_TRANSLATE("Strike out"), msg);
			i->SetTarget(this);
			menu->AddItem(i);
		}

		// a note or text goes where the menu was opened
		BPoint clicked = CorrectMousePos(ConvertFromScreen(point));
		fz_point where = mPage->DevToPage(clicked);
		bool onPage = clicked.x >= 0 && clicked.y >= 0 && clicked.x < mWidth && clicked.y < mHeight;
		static const struct { const char* label; PlacementTool tool; } kTools[] = {
			{ B_TRANSLATE_MARK("Note"), kToolNote }, { B_TRANSLATE_MARK("Text"), kToolFreeText },
			{ B_TRANSLATE_MARK("Rectangle"), kToolRectangle }, { B_TRANSLATE_MARK("Ellipse"), kToolEllipse },
			{ B_TRANSLATE_MARK("Line"), kToolLine }, { B_TRANSLATE_MARK("Arrow"), kToolArrow },
			{ B_TRANSLATE_MARK("Drawing"), kToolInk }
		};
		BMenu* add = new BMenu(B_TRANSLATE("Add"));
		for (size_t t = 0; t < sizeof(kTools) / sizeof(kTools[0]); t++) {
			msg = new BMessage(ADD_TOOL_MSG);
			msg->AddInt32("tool", kTools[t].tool);
			if (onPage && (kTools[t].tool == kToolNote || kTools[t].tool == kToolFreeText)) {
				msg->AddFloat("x", where.x);
				msg->AddFloat("y", where.y);
			}
			i = new BMenuItem(B_TRANSLATE_NOCOLLECT(kTools[t].label), msg);
			i->SetTarget(this);
			add->AddItem(i);
		}
		if (mDoc->CanDrawAnnotations())
			menu->AddItem(add);
		else
			delete add;

		if (annotation != NULL) {
			if (annotation->hasColor && !annotation->isFreeText) {
				BMessage change(CHANGE_COLOR_MSG);
				change.AddInt32("page", ActivePage());
				change.AddInt32("index", annotation->index);
				menu->AddItem(BuildColorMenu(B_TRANSLATE("Color"), change, this, annotation->hasColor,
					annotation->color));
			}

			// the line and the fill of a shape or a drawing
			const bool shape = annotation->kind == kAnnotRectangle || annotation->kind == kAnnotEllipse
				|| annotation->kind == kAnnotLine || annotation->kind == kAnnotInk;
			if (shape && annotation->width > 0) {
				BMenu* widths = new BMenu(B_TRANSLATE("Line width"));
				static const float kWidths[] = { 1, 2, 3, 4, 6, 8 };
				for (size_t w = 0; w < sizeof(kWidths) / sizeof(kWidths[0]); w++) {
					BMessage* change = new BMessage(CHANGE_STYLE_MSG);
					change->AddInt32("page", ActivePage());
					change->AddInt32("index", annotation->index);
					change->AddFloat("width", kWidths[w]);
					change->AddBool("hasFill", annotation->hasFill);
					change->AddInt32("color", (int32)annotation->fill);
					BString label;
					label.SetToFormat(B_TRANSLATE("%d points"), (int)kWidths[w]);
					i = new BMenuItem(label.String(), change);
					i->SetTarget(this);
					i->SetMarked(fabsf(annotation->width - kWidths[w]) < 0.5f);
					widths->AddItem(i);
				}
				menu->AddItem(widths);
			}
			if (shape && (annotation->kind == kAnnotRectangle || annotation->kind == kAnnotEllipse)) {
				BMessage fillTemplate(CHANGE_STYLE_MSG);
				fillTemplate.AddInt32("page", ActivePage());
				fillTemplate.AddInt32("index", annotation->index);
				fillTemplate.AddFloat("width", annotation->width);
				fillTemplate.AddBool("hasFill", true);
				BMenu* fill = BuildColorMenu(B_TRANSLATE("Fill"), fillTemplate, this, annotation->hasFill, annotation->fill);
				BMessage* none = new BMessage(CHANGE_STYLE_MSG);
				none->AddInt32("page", ActivePage());
				none->AddInt32("index", annotation->index);
				none->AddFloat("width", annotation->width);
				none->AddBool("hasFill", false);
				none->AddInt32("color", 0);
				i = new BMenuItem(B_TRANSLATE("No fill"), none);
				i->SetTarget(this);
				i->SetMarked(!annotation->hasFill);
				fill->AddItem(i, 0);
				fill->AddItem(new BSeparatorItem(), 1);
				menu->AddItem(fill);
			}

			msg = new BMessage(EDIT_NOTE_MSG);
			msg->AddInt32("page", ActivePage());
			msg->AddInt32("index", annotation->index);
			msg->AddString("text", annotation->contents);
			i = new BMenuItem(annotation->isFreeText ? B_TRANSLATE("Edit text" B_UTF8_ELLIPSIS)
				: B_TRANSLATE("Edit note" B_UTF8_ELLIPSIS), msg);
			i->SetTarget(this);
			menu->AddItem(i);

			// the annotation as a Web Annotation (JSON-LD), to hand it on
			msg = new BMessage(COPY_ANNOTATION_MSG);
			msg->AddInt32("page", ActivePage());
			msg->AddInt32("index", annotation->index);
			i = new BMenuItem(B_TRANSLATE("Copy as Web Annotation"), msg);
			i->SetTarget(this);
			menu->AddItem(i);

			msg = new BMessage(DELETE_ANNOTATION_MSG);
			msg->AddInt32("page", ActivePage());
			msg->AddInt32("index", annotation->index);
			i = new BMenuItem(B_TRANSLATE("Delete annotation"), msg);
			i->SetTarget(this);
			menu->AddItem(i);
		}
	}

	if (link != NULL) {
		menu->AddSeparatorItem();

		// Open document in new window
		if (IsLinkToDocument(link, &s)) {
			msg = new BMessage(OPEN_FILE_MSG);
			msg->AddString("file", s);
			i = new BMenuItem(B_TRANSLATE("Open in new window"), msg);
			i->SetTarget(this);
			menu->AddItem(i);
		}

		// Copy link location
		msg = new BMessage(COPY_LINK_MSG);
		LinkToString(link, &s);
		msg->AddString("link", s);
		i = new BMenuItem(B_TRANSLATE("Copy link"), msg);
		i->SetTarget(this);
		menu->AddItem(i);
	}

	point -= BPoint(10, 10);
	menu->Go(point, true, false, false);
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::LinkToString(const DocLink* link, BString* string) {
	if (link == NULL) {
		string->Truncate(0);
		return;
	}

	int page;
	float x, y;
	if (mDoc->ResolveLink(link->uri.String(), &page, &x, &y)) {
		char label[128];
		snprintf(label, sizeof(label), B_TRANSLATE("Go to page %d"), page);
		*string = label;
	} else if (link->uri.Length() == 0) {
		*string = B_TRANSLATE("[unknown link]");
	} else {
		*string = link->uri;
	}
}


// the text of a note for a tooltip: broken into lines, shortened if it is very long, with the author
static BString
NoteTipText(const DocAnnotation* annotation)
{
	const int kColumns = 60, kMaxLength = 800;
	BString text = annotation->contents;
	text.ReplaceAll("\r\n", "\n");
	text.ReplaceAll("\r", "\n");
	if (text.CountChars() > kMaxLength) {
		text.TruncateChars(kMaxLength);
		text << B_UTF8_ELLIPSIS;
	}

	BString result;
	int column = 0;
	const char* p = text.String();
	while (*p != '\0') {
		// the next word
		const char* end = p;
		while (*end != '\0' && *end != ' ' && *end != '\n')
			end++;
		BString word(p, end - p);
		int length = word.CountChars();
		if (column > 0 && column + 1 + length > kColumns) {
			result << '\n';
			column = 0;
		}
		if (column > 0 && *(p - 1) == ' ') {
			result << ' ';
			column++;
		}
		result << word;
		column += length;
		if (*end == '\n') {
			result << '\n';
			column = 0;
		}
		p = *end == '\0' ? end : end + 1;
	}

	if (annotation->author.Length() > 0)
		result << "\n\xe2\x80\x94 " << annotation->author;
	return result;
}


void
PDFView::DisplayLink(BPoint point)
{
	BString str;
	// the page the mouse is over, as the active one while this goes on
	SlotScope hover(this, SlotAt(point));
	if (mRendering || mDragStarted || (mDoc == NULL) || (mDoc->PageCount() == 0))
		return;

	if (mTool != kToolNone) {
		SetViewCursor(mToolCursor);
		return;
	}
	if (mMarkupArmed) {
		SetViewCursor(gApp->textSelectionCursor);
		return;
	}

	if (const DocAnnotation* selected = SelectedAnnotation()) {
		int handle = HandleAt(selected, CorrectMousePos(point));
		if (handle != kHandleNone) {
			SetViewCursor(mHandleCursors[handle]);
			return;
		}
	}

	BPoint p = CorrectMousePos(point);
	// over selection?
	if (((mSelected == SELECTED) && mActive->number == mInteractionPage && InSelection(point)) ||
		p.x < 0 || p.y < 0 || p.x >= mWidth || p.y >= mHeight) {
		SetViewCursor((BCursor*)B_CURSOR_SYSTEM_DEFAULT);
		return;
	}

	// selecting?
	if (SelectModifierDown()) {
		SetViewCursor(gApp->textSelectionCursor);
		if (mNoteTip != 0) {
			mNoteTip = 0;
			mNoteReady = false;
			SetToolTip("");
			HideToolTip();
		}
		return;
	}

	// a note of an annotation is shown as a tooltip, to read it while scrolling through the document
	const DocAnnotation* note = OnAnnotation(point);
	if (note == NULL)
		note = MarginNoteAt(p);
	if (note != NULL && note->contents.Length() > 0) {
		if (mNoteTip != note->index + 1) {
			mNoteTip = note->index + 1;
			mNoteHoverSince = system_time();
			mNoteReady = false;
			mLink = NULL;
			SetToolTip(NoteTipText(note).String());		// (shown after the usual delay)
		}
		// after the delay the note can be edited by a click: the cursor says so
		SetViewCursor(mNoteReady ? gApp->linkCursor : gApp->handCursor);
		return;
	}
	if (mNoteTip != 0) {
		mNoteTip = 0;
		mNoteReady = false;
		SetToolTip("");
		HideToolTip();
	}

	// over link?
	const DocLink* link;
	if ((link = OnLink(point)) != NULL) {
		// new link?
		if (link != mLink) {
			SetViewCursor(gApp->linkCursor);
			mLink = link;
			LinkToString(link, &str);
			SetToolTip( str.String() );
			ShowToolTip();
		}
	} else {
		if (mLink) {
			mLink = NULL;
			SetToolTip("");
			HideToolTip();
		}
		SetViewCursor(gApp->handCursor);
	}
}

///////////////////////////////////////////////////////////////////////////
// Slots: the pages that are shown

PageSlot::PageSlot()
	:
	number(0),
	page(new CachedPage()),
	renderer(new PageRenderer()),
	rendererId(-1),
	rendering(false),
	origin(0, 0),
	dpi(-1),
	rotation(0)
{
}


void
PageSlot::Retire()
{
	renderer->Retire(page);
	renderer = new PageRenderer();
	page = new CachedPage();
	rendering = false;
	rendererId = -1;
}


PageSlot::~PageSlot()
{
	renderer->Abort();
	renderer->Wait();
	delete renderer;
	delete page;
}


PDFView::SlotScope::SlotScope(PDFView* view, PageSlot* slot)
	:
	fView(view),
	fSaved(view->mActive)
{
	if (slot != NULL)
		view->SetActiveRaw(slot);
}


PDFView::SlotScope::~SlotScope()
{
	fView->SetActiveRaw(fSaved);
}


// a slot for a page, one that is not in use if there is one (the active page may stand for it, that is no matter)
PageSlot*
PDFView::NewSlot()
{
	PageSlot* slot;
	if (!mFreeSlots.empty()) {
		slot = mFreeSlots.back();
		mFreeSlots.pop_back();
	} else
		slot = new PageSlot();

	if (Window() != NULL)
		slot->renderer->SetListener(Window(), this);
	slot->renderer->SetDocument(mDoc);
	return slot;
}


// the page is not shown any more, the slot waits to be used again
void
PDFView::ReleaseSlot(PageSlot* slot)
{
	// (a page that is rendered is let go of without waiting for it)
	if (slot->renderer->IsRunning())
		RetireSlot(slot);
	else {
		slot->renderer->Abort();
		slot->renderer->Wait();
	}
	slot->page->MakeEmpty();
	slot->number = 0;
	slot->rendering = false;
	slot->rendererId = -1;
	for (size_t i = 0; i < mSlots.size(); i++) {
		if (mSlots[i] == slot) {
			mSlots.erase(mSlots.begin() + i);
			break;
		}
	}
	mFreeSlots.push_back(slot);
}


// The renderer and the cached page of a slot go on without it; the slot is given new ones (which the view that points
// to the active slot has to be told about).
void
PDFView::RetireSlot(PageSlot* slot)
{
	slot->Retire();
	if (Window() != NULL)
		slot->renderer->SetListener(Window(), this);
	slot->renderer->SetDocument(mDoc);
	if (slot == mActive)
		SetActiveRaw(slot);
}


// The slots work for another document. Pages that are rendered of the old one are not waited for.
void
PDFView::SetSlotsDocument(Document* document)
{
	for (size_t i = 0; i < mSlots.size(); i++) {
		PageSlot* slot = mSlots[i];
		if (slot->renderer->IsRunning()) {
			slot->Retire();
			if (Window() != NULL)
				slot->renderer->SetListener(Window(), this);
			if (slot == mActive)
				SetActiveRaw(slot);
		}
		slot->renderer->SetDocument(document);
		if (document == NULL)
			slot->page->MakeEmpty();
	}
	for (size_t i = 0; i < mFreeSlots.size(); i++) {
		mFreeSlots[i]->renderer->SetDocument(document);
		if (document == NULL)
			mFreeSlots[i]->page->MakeEmpty();
	}
	if (document == NULL) {
		// nothing is shown of the document that goes away
		while (!mSlots.empty())
			ReleaseSlot(mSlots.back());
		if (mActive != NULL)
			mActive->page->MakeEmpty();
	}
}


PageSlot*
PDFView::SlotForPage(int page) const
{
	for (size_t i = 0; i < mSlots.size(); i++) {
		if (mSlots[i]->number == page)
			return mSlots[i];
	}
	return NULL;
}


// the page that is at the point (in the view), none if the point is beside the pages
PageSlot*
PDFView::SlotAt(BPoint point) const
{
	for (size_t i = 0; i < mSlots.size(); i++) {
		const PageSlot* slot = mSlots[i];
		BRect area(slot->origin.x, slot->origin.y, slot->origin.x + slot->page->GetWidth() - 1,
			slot->origin.y + slot->page->GetHeight() - 1);
		if (area.Contains(point))
			return mSlots[i];
	}
	return NULL;
}


// what the members mPage, mBitmap and the others stand for
void
PDFView::SetActiveRaw(PageSlot* slot)
{
	mActive = slot;
	mPage = slot->page;
	mBitmap = slot->page->GetBitmap();
	mWidth = slot->page->GetWidth();
	mHeight = slot->page->GetHeight();
	mLeft = slot->origin.x;
	mTop = slot->origin.y;
	mRendering = slot->rendering;
}


// the page of a click becomes the active one; the selections belong to the page they were made on
void
PDFView::ActivateSlot(PageSlot* slot)
{
	if (slot == NULL || (slot == mActive && slot->number == mInteractionPage))
		return;

	if (mActive != NULL && slot->number != mInteractionPage) {
		// the old page is still the active one, the selections are taken away from it
		if (mSelected != NOT_SELECTED)
			SelectNone();
		if (mAnnotationIndex >= 0)
			SelectAnnotation(-1);
	}
	SetActiveRaw(slot);
	mInteractionPage = slot->number;
}


void
PDFView::StartRender(PageSlot* slot, bool keepImage)
{
	// (while a book is laid out again the pages are not what they will be; they are rendered when it is done)
	if (mLayingOut) {
		slot->rendering = false;
		return;
	}
	slot->rendering = true;
	slot->dpi = GetZoomDPI();
	slot->rotation = mRotation;
	slot->renderer->Start(slot->page, slot->number, GetZoomDPI(), mRotation, &slot->rendererId, keepImage);
}


// Sizes of the pages for the zoom and the rotation, the canvas that is scrolled over and the places of the pages.
void
PDFView::Relayout()
{
	mLayout.SetPages(mDoc, mDoc != NULL ? mDoc->PageCount() : 0, GetZoomDPI(), (int)mRotation);
	mCurrentPage = mLayout.NormalizePage(mCurrentPage);
	mLayout.Arrange(mCurrentPage);
	mCanvasWidth = mLayout.CanvasWidth();
	mCanvasHeight = mLayout.CanvasHeight();
}


// Makes the slots those of the pages that are needed: the page or the spread, the pages that are in view and some
// around them in a continuous flow. New slots are rendered. The pages go to the middle of the view if all of them
// fit into it.
void
PDFView::SyncSlots()
{
	if (mDoc == NULL || mDoc->PageCount() < 1)
		return;

	BRect bounds(Bounds());
	mCanvasLeft = bounds.Width() + 1 > mCanvasWidth ? floorf((bounds.Width() - mCanvasWidth) / 2) : 0;
	mCanvasTop = bounds.Height() + 1 > mCanvasHeight ? floorf((bounds.Height() - mCanvasHeight) / 2) : 0;

	std::vector<int> needed;
	mLayout.NeededPages(bounds.top - mCanvasTop, bounds.bottom - mCanvasTop, bounds.Height(), needed);

	// the page with a selection stays as long as it is selected
	if (mLayout.IsContinuous() && mInteractionPage > 0
		&& (mSelected != NOT_SELECTED || mAnnotationIndex >= 0)
		&& std::find(needed.begin(), needed.end(), mInteractionPage) == needed.end())
		needed.push_back(mInteractionPage);

	// slots of pages that are not needed any more
	for (int i = (int)mSlots.size() - 1; i >= 0; i--) {
		if (std::find(needed.begin(), needed.end(), mSlots[i]->number) == needed.end())
			ReleaseSlot(mSlots[i]);
	}

	// slots for the pages that are needed, in the order of the pages
	for (size_t i = 0; i < needed.size(); i++) {
		if (SlotForPage(needed[i]) != NULL)
			continue;
		PageSlot* slot = NewSlot();
		slot->number = needed[i];
		size_t at = 0;
		while (at < mSlots.size() && mSlots[at]->number < slot->number)
			at++;
		mSlots.insert(mSlots.begin() + at, slot);
		StartRender(slot);
	}

	for (size_t i = 0; i < mSlots.size(); i++) {
		BRect rect = mLayout.PageRect(mSlots[i]->number);
		mSlots[i]->origin = BPoint(mCanvasLeft + rect.left, mCanvasTop + rect.top);
	}

	// the active page is one of those that are shown: the one of the selections, the current one or the first
	PageSlot* wanted = SlotForPage(mInteractionPage);
	if (wanted == NULL)
		wanted = SlotForPage(mCurrentPage);
	if (wanted == NULL && !mSlots.empty())
		wanted = mSlots.front();
	if (wanted != NULL) {
		if (mInteractionPage != wanted->number) {
			// the page that the selections belong to is not shown any more
			if (mSelected != NOT_SELECTED) {
				mSelected = NOT_SELECTED;
				ClearQuads();
			}
			mAnnotationIndex = -1;
			mInteractionPage = wanted->number;
		}
		SetActiveRaw(wanted);
	}
}


///////////////////////////////////////////////////////////////////////////
// Shows everything anew: new sizes, canvas and places, and all pages that are shown are rendered again.
void
PDFView::Redraw(bool keepRendered)
{
	if (!mTurnBegin)
		CancelTurn();

	PDFWindow* parentWin = GetPDFWindow();

	mMouseWheelDY = 0;

	// abort rendering process if neccesary and wait for it to finish
	WaitForPage(true);

	// in a continuous flow the page at the top stays at the top when the size of the pages changes
	int anchorPage = 0;
	float anchorFraction = 0;
	if (mLayout.IsContinuous() && mDoc != NULL && !mSlots.empty()) {
		anchorPage = mCurrentPage;
		float width, height;
		mLayout.PageSize(anchorPage, &width, &height);
		if (height > 0)
			anchorFraction = (Bounds().top - mCanvasTop - mLayout.PageTop(anchorPage)) / height;
	}

	Relayout();
	if (mFitWidthPending && Window() != NULL && Bounds().Width() > 100 && mCanvasWidth > 0) {
		mFitWidthPending = false;
		int32 zoomOld = GetZoomDPI();
		int32 zoomNew = (int32)(Bounds().Width() * zoomOld / mCanvasWidth);
		if (zoomNew != zoomOld && zoomNew >= ZOOM_DPI_MIN && zoomNew <= ZOOM_DPI_MAX) {
			mZoom = -zoomNew;
			if (parentWin)
				parentWin->SetZoom(-zoomNew);
			Relayout();
		}
	}
	if (parentWin)
		parentWin->NewPage(mCurrentPage);

	// A selection stays through zoom and rotation, it belongs to the page. Text is kept in page space, an
	// area is converted to page space here and back to the new zoom below.
	PageSlot* selectionSlot = SlotForPage(mInteractionPage);
	bool pageStays = mRenderedPage != 0 && selectionSlot != NULL && mLayout.PageRect(mInteractionPage).IsValid();
	bool keepSelection = pageStays && mSelected == SELECTED;
	bool keepArea = keepSelection && mSelectionKind == kSelectArea;
	fz_rect areaOnPage = fz_empty_rect;
	if (keepArea) {
		fz_point a = selectionSlot->page->DevToPage(mSelection.LeftTop());
		fz_point b = selectionSlot->page->DevToPage(mSelection.RightBottom());
		areaOnPage = fz_make_rect(fminf(a.x, b.x), fminf(a.y, b.y), fmaxf(a.x, b.x), fmaxf(a.y, b.y));
	}
	if (!keepSelection) {
		mSelected = NOT_SELECTED;
		ClearQuads();
	}
	if (!pageStays)
		mAnnotationIndex = -1;
	mRenderedPage = mCurrentPage;

	// every page is rendered again, the slots keep their bitmaps
	for (int i = (int)mSlots.size() - 1; i >= 0; i--) {
		PageSlot* slot = mSlots[i];
		bool stays = keepRendered && slot->page->GetState() == CachedPage::READY && slot->dpi == GetZoomDPI()
			&& slot->rotation == mRotation;
		if (!stays)
			ReleaseSlot(slot);
	}
	mLink = NULL;
	SyncSlots();
	if (keepArea) {
		if (PageSlot* slot = SlotForPage(mInteractionPage))
			mSelection = slot->page->PageToDev(areaOnPage);
	}

	FixScrollbars();
	if (anchorPage > 0) {
		float width, height;
		mLayout.PageSize(anchorPage, &width, &height);
		ScrollTo(Bounds().left, mCanvasTop + mLayout.PageTop(anchorPage) + anchorFraction * height);
	} else if (mLayout.IsContinuous() && mDoc != NULL && mCurrentPage > 1) {
		// the flow has just become continuous (or the document is new): the view goes to the current page
		ScrollToPage(mCurrentPage, true);
	}

	if (parentWin) {
		parentWin->GetFileAttributes()->SetPage(mCurrentPage);
		parentWin->SetPage (mCurrentPage);
		parentWin->SetZoomSize (mCanvasWidth, mCanvasHeight);
	}

	Invalidate();
}

///////////////////////////////////////////////////////////
void
PDFView::RestartDoc() {
	WaitForPage(true);
	mRenderedPage = 0;
	Redraw();
}


///////////////////////////////////////////////////////////////////////////
void
PDFView::FixScrollbars ()
{
	BRect frame = Bounds();
	BScrollBar * scroll;
	float x, y;
	float bigStep, smallStep;

	x = mCanvasWidth - frame.Width();
	if (x < 0.0) {
		x = 0.0;
	}
	y = mCanvasHeight - frame.Height();
	if (y < 0.0) {
		y = 0.0;
	}

	scroll = ScrollBar (B_HORIZONTAL);
	scroll->SetRange (0.0, x);
	scroll->SetProportion ((mCanvasWidth - x) / mCanvasWidth);
	bigStep = frame.Width() - 2;
	smallStep = bigStep / 10.;
	scroll->SetSteps (smallStep, bigStep);

	scroll = ScrollBar (B_VERTICAL);
	scroll->SetRange (0.0, y);
	scroll->SetProportion ((mCanvasHeight - y) / mCanvasHeight);
	bigStep = frame.Height() - 2;
	smallStep = bigStep / 10.;
	scroll->SetSteps (smallStep, bigStep);
}

///////////////////////////////////////////////////////////////////////////
// a page has been rendered
void
PDFView::PostRedraw(thread_id id, BBitmap *bitmap) {
	if (id == -1)
		return;

	for (size_t i = 0; i < mSlots.size(); i++) {
		PageSlot* slot = mSlots[i];
		if (slot->rendererId != id)
			continue;
		slot->rendering = false;
		slot->rendererId = -1;
		if (slot == mActive)
			mRendering = false;
		TimingMark("page rendered");
		UpdateFindQuads(slot);
		TimingMark("find quads updated");
		Invalidate(BRect(slot->origin.x - 1, slot->origin.y - 1, slot->origin.x + slot->page->GetWidth(),
			slot->origin.y + slot->page->GetHeight()));
		break;
	}
	if (mTurnState == kTurnWaiting && SlotsReady())
		TurnReady();

	BPoint mouse; uint32 buttons;
	GetMouse(&mouse, &buttons);
	DisplayLink(mouse);
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::RedrawAborted(thread_id id, BBitmap *bitmap) {
	if (id == -1)
		return;
	for (size_t i = 0; i < mSlots.size(); i++) {
		if (mSlots[i]->rendererId == id) {
			mSlots[i]->rendering = false;
			mSlots[i]->rendererId = -1;
			if (mSlots[i] == mActive)
				mRendering = false;
			break;
		}
	}
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::WaitForPage(bool abort) {
	for (size_t i = 0; i < mSlots.size(); i++) {
		if (abort)
			mSlots[i]->renderer->Abort();
		mSlots[i]->renderer->Wait();
	}
	if (abort) {
		for (size_t i = 0; i < mSlots.size(); i++) {
			mSlots[i]->rendering = false;
			mSlots[i]->rendererId = -1;
		}
		mRendering = false;
	}
}

///////////////////////////////////////////////////////////////////////////
// the places of the pages (in the middle of the view if the view is larger)
void
PDFView::CenterPage() {
	SyncSlots();
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::Resize() {
	FixScrollbars();
	// When the window is resized the scroll bars reset the view to the top (before it is told about the new size). The place
	// that was scrolled to stays, and with it the page.
	BPoint kept(mKeptLeft, mKeptTop);
	if (Bounds().LeftTop() != kept)
		ScrollTo(kept.x, kept.y);
	SyncSlots();
	mKeptLeft = Bounds().left;		// (what the new size makes of it)
	mKeptTop = Bounds().top;
	if (BScrollBar* bar = ScrollBar(B_VERTICAL))
		bar->SetValue(mKeptTop);
	if (BScrollBar* bar = ScrollBar(B_HORIZONTAL))
		bar->SetValue(mKeptLeft);
	UpdateVisibleSlots();
	Invalidate();
}

///////////////////////////////////////////////////////////////////////////
// What the page box shows has changed (in a continuous flow by scrolling).
void
PDFView::NotifyPageChanged()
{
	PDFWindow* parentWin = GetPDFWindow();
	if (parentWin == NULL)
		return;
	parentWin->NewPage(mCurrentPage);
	parentWin->GetFileAttributes()->SetPage(mCurrentPage);
	parentWin->SetPage(mCurrentPage);
}

///////////////////////////////////////////////////////////////////////////
// After scrolling in a continuous flow: the pages that come into view are rendered, the others are let go, and
// the page at the top is the current one.
void
PDFView::UpdateVisibleSlots()
{
	if (!mLayout.IsContinuous() || mDoc == NULL || mDoc->PageCount() < 1)
		return;

	// the page at the top is the current one, which decides what the active page falls back to
	int page = mLayout.PageAt(Bounds().top - mCanvasTop + 40);
	bool changed = page != mCurrentPage;
	mCurrentPage = page;
	SyncSlots();
	if (changed)
		NotifyPageChanged();
}

///////////////////////////////////////////////////////////////////////////
// continuous flow: the top (or the bottom) of the page is at the top of the view
void
PDFView::ScrollToPage(int page, bool top)
{
	float width, height;
	mLayout.PageSize(page, &width, &height);
	float y = mCanvasTop + mLayout.PageTop(page);
	if (!top)
		y += height - Bounds().Height();
	ScrollTo(Bounds().left, y);
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::SetPage (int page)
{
	if (mDoc == NULL)
		return;
	if (!mTurnBegin)
		CancelTurn();

	mSelected = NOT_SELECTED;

	int requested = page;
	if (requested < 1)
		requested = 1;
	else if (requested > GetNumPages())
		requested = GetNumPages();
	int target = mLayout.NormalizePage(requested);

	if (mLayout.IsContinuous()) {
		mCurrentPage = requested;
		ScrollToPage(requested, true);
		mCurrentPage = requested;	// whatever the scrolling made of it
		NotifyPageChanged();
	} else if (mCurrentPage != target) {
		mCurrentPage = target;
		Redraw ();
	}

	// the page that was asked for is the active one (in a spread it can be the right one)
	if (PageSlot* slot = SlotForPage(requested))
		ActivateSlot(slot);
}

//////////////////////////////////////////////////////////////////
void
PDFView::MoveToPage(int page, bool top) {
	if (page > GetNumPages()) page = GetNumPages();
	if (page <= 0) page = 1;
	bool notChanged = mCurrentPage == mLayout.NormalizePage(page);
	if (notChanged) return;

	RecordHistory();

	if (mLayout.IsContinuous()) {
		ScrollToPage(page, top);
		mCurrentPage = page;
		NotifyPageChanged();
		return;
	}

	// the fancy mode: the pages of what is shown now are kept for the turn
	bool turn = TurnWanted(page);
	if (turn) {
		mTurnBegin = true;
		BeginTurn(page);
	} else
		CancelTurn();

	BRect bounds(Bounds());
	ScrollTo(bounds.left, top ? 0 : mCanvasHeight);
	SetPage(page);

	if (turn) {
		mTurnBegin = false;
		if (mTurnState == kTurnWaiting && SlotsReady())
			TurnReady();
	}
}

//////////////////////////////////////////////////////////////////
void
PDFView::NextPage()
{
	MoveToPage(mLayout.NextSpreadPage(mCurrentPage));
}

//////////////////////////////////////////////////////////////////
void
PDFView::PreviousPage()
{
	MoveToPage(mLayout.PreviousSpreadPage(mCurrentPage));
}

//////////////////////////////////////////////////////////////////
// The title page alone or with the page after it: the spreads change, the current page stays in view.
void
PDFView::SetTitlePageAlone(bool alone)
{
	if (alone == mLayout.FirstPageAlone() || mDoc == NULL)
		return;

	WaitForPage(true);
	mLayout.SetFirstPageAlone(alone);
	gApp->GetSettings()->SetTitlePageAlone(alone);
	mRenderedPage = 0;
	Redraw(true);
	PDFWindow* w = GetPDFWindow();
	if (w)
		w->UpdateInputEnabler();
}

//////////////////////////////////////////////////////////////////
// Read from the right to the left (a manga): the spreads are mirrored, the current page stays in view. The choice is
// kept with the file.
void
PDFView::SetRightToLeft(bool rightToLeft)
{
	if (rightToLeft == mLayout.RightToLeft() || mDoc == NULL)
		return;

	WaitForPage(true);
	mLayout.SetRightToLeft(rightToLeft);
	if (PDFWindow* w = GetPDFWindow())
		w->GetFileAttributes()->SetReading(rightToLeft ? 1 : 0);
	mRenderedPage = 0;
	Redraw(true);
	if (PDFWindow* w = GetPDFWindow())
		w->UpdateInputEnabler();
}


//////////////////////////////////////////////////////////////////
// A webtoon is read from the top to the bottom: all pages below each other, no gap between them, as wide as the window. Off, the
// flow that the reader chose is back. The choice is kept with the file.
void
PDFView::SetTopToBottom(bool topToBottom)
{
	if (topToBottom == mLayout.TopToBottom() || mDoc == NULL)
		return;

	WaitForPage(true);
	mLayout.SetTopToBottom(topToBottom);
	mLayout.SetFlow(topToBottom ? kFlowContinuous : (PageFlow)gApp->GetSettings()->GetPageFlow());
	if (mLayout.RightToLeft() && topToBottom)
		mLayout.SetRightToLeft(false);
	if (PDFWindow* w = GetPDFWindow())
		w->GetFileAttributes()->SetReading(topToBottom ? 2 : 0);
	mFitWidthPending = topToBottom;
	BView::ScrollTo(BPoint(0, 0));
	mRenderedPage = 0;
	Redraw(true);
	if (mLayout.IsContinuous())
		ScrollToPage(mCurrentPage, true);
	if (PDFWindow* w = GetPDFWindow())
		w->UpdateInputEnabler();
}


//////////////////////////////////////////////////////////////////
// How the pages are arranged: the current page stays the current one.
void
PDFView::SetFlow(PageFlow flow)
{
	if (flow == mLayout.Flow() || mDoc == NULL)
		return;

	TimingStart();
	WaitForPage(true);
	TimingMark("flow: render aborted");
	mLayout.SetFlow(flow);
	gApp->GetSettings()->SetPageFlow((int)flow);
	// the scroll position belonged to the other arrangement, which is not arranged for this one yet
	BView::ScrollTo(BPoint(0, 0));
	mRenderedPage = 0;
	Redraw(true);
	TimingMark("flow: redrawn (renders started)");
	if (mLayout.IsContinuous())
		ScrollToPage(mCurrentPage, true);
	PDFWindow* w = GetPDFWindow();
	if (w)
		w->UpdateInputEnabler();
	TimingMark("flow: done");
}

//////////////////////////////////////////////////////////////////
void
PDFView::BeginHistoryNavigation() {
	// Note: We are going into history navigation mode and
	// have to store the current position state in the history.
	if (mNavigationState == kNotInHistory) {
		mNavigationState = kInHistory;
		BRect bounds(Bounds());
		mHistory.AddPosition(mCurrentPage, mZoom, bounds.left, bounds.top, mRotation);
	}
}

//////////////////////////////////////////////////////////////////
void
PDFView::EndHistoryNavigation() {
	// Note: When not navigating through the history (= kNotInHistory) the
	// current position information is not stored in the history.
	// The state is stored prior to changes of the state to the history.
	// When in navigating through the history (kInHistory), the restored
	// state is the top of the history. This state has to be replaced
	// when we record the current state (= Back()).
	if (mNavigationState == kInHistory) {
		mNavigationState = kNotInHistory;
		mHistory.Back();
	}
}

//////////////////////////////////////////////////////////////////
// The document is open: from here on the places that are left are recorded, and the first of them is where the document was
// opened (the saved page).
void
PDFView::HistoryStart() {
	if (mNavigationState == kNotInHistory)
		mHistory.ClearPositions();
	mHistoryOpen = true;
}

//////////////////////////////////////////////////////////////////
void
PDFView::RecordHistory() {
	// (nothing is recorded while the document opens: its zoom and rotation are set at the first page)
	if (!mHistoryOpen)
		return;
	EndHistoryNavigation();
	BRect bounds(Bounds());
	mHistory.AddPosition(mCurrentPage, mZoom, bounds.left, bounds.top, mRotation);
}

//////////////////////////////////////////////////////////////////
void
PDFView::RecordHistory(entry_ref ref, const char* ownerPassword, const char* userPassword) {
	// XXX: record file open events too, otherwise they are missed if page state does not change between
	// open events.
	mHistory.SetFile(ref, ownerPassword, userPassword);
}

//////////////////////////////////////////////////////////////////
void
PDFView::RestoreHistory() {
	HistoryEntry* e = mHistory.GetTop();
	if (e == NULL) return;

	HistoryPosition* pos = dynamic_cast<HistoryPosition*>(e);
	if (pos) {
		PDFWindow* w = GetPDFWindow();
		HistoryFile* file = pos->GetFile();
		entry_ref ref = file->GetRef();

		if (w && !w->IsCurrentFile(&ref)) {
			bool encrypted;
			w->LoadFile(&ref, file->GetOwnerPassword(), file->GetUserPassword(), &encrypted);
			return;
		}

		int page; int32 left, top;
		page = pos->GetPage();
		mZoom = pos->GetZoom();
		left = pos->GetLeft();
		top = pos->GetTop();
		mRotation = pos->GetRotation();

		if (w) {
			w->SetZoom(mZoom); w->SetRotation(mRotation);
		}
		mCurrentPage = -1;
		SetPage(page);
		ScrollTo(left, top);
	}
}

//////////////////////////////////////////////////////////////////
void
PDFView::Back() {
	BeginHistoryNavigation();
	if (mHistory.Back()) {
		RestoreHistory();
	}
}
//////////////////////////////////////////////////////////////////
void
PDFView::Forward() {
	BeginHistoryNavigation();
	if (mHistory.Forward()) {
		RestoreHistory();
	}
}

//////////////////////////////////////////////////////////////////
void
PDFView::SetZoom (int zoom)
{
	if (mZoom != zoom) {
		RecordHistory();
		mZoom = zoom;
		Redraw ();
	}
}

//////////////////////////////////////////////////////////////////
void
PDFView::Zoom(bool zoomIn) {
	int32 zoomOld = GetZoomDPI();
	int32 zoomNew;
	if (zoomIn) {
		zoomNew = (int32)(zoomOld * 1.2);
		if (zoomNew > ZOOM_DPI_MAX) zoomNew = ZOOM_DPI_MAX;
	} else {
		zoomNew = (int32)(zoomOld / 1.2);
		if (zoomNew < ZOOM_DPI_MIN) zoomNew = ZOOM_DPI_MIN;
	}
	if (zoomNew != zoomOld) {
		SetZoom(-zoomNew);
		PDFWindow* w = GetPDFWindow();
		if (w) w->SetZoom(-zoomNew);
	}
}

//////////////////////////////////////////////////////////////////
void
PDFView::FitToPageWidth() {
	BRect r(Bounds());
	int32 zoomOld = GetZoomDPI();
	int32 zoomNew = (int32)(r.Width() * zoomOld / mCanvasWidth);
	if (zoomOld != zoomNew) {
		SetZoom(-zoomNew);
		PDFWindow* w = GetPDFWindow();
		if (w) w->SetZoom(-zoomNew);
	}
}

//////////////////////////////////////////////////////////////////
void
PDFView::FitToPage() {
	BRect r(Bounds());
	int32 zoomOld = GetZoomDPI();
	// the page, the spread; in a flow of all pages the page that is current
	float fitWidth = mCanvasWidth, fitHeight = mCanvasHeight;
	if (mLayout.IsContinuous())
		mLayout.PageSize(mCurrentPage, &fitWidth, &fitHeight);
	int32 zoomNewH = (int32)(r.Width() * zoomOld / fitWidth);
	int32 zoomNewV = (int32)(r.Height() * zoomOld / fitHeight);
	int32 zoomNew = (zoomNewH < zoomNewV) ? zoomNewH : zoomNewV;
	if (zoomOld != zoomNew) {
		SetZoom(-zoomNew);
		PDFWindow* w = GetPDFWindow();
		if (w) w->SetZoom(-zoomNew);
	}
}

//////////////////////////////////////////////////////////////////
int16
PDFView::GetZoomDPI() const {
	if (mZoom >= MIN_ZOOM)
		return kZoomDPI[mZoom -  MIN_ZOOM];
	else
		return -mZoom;
}

//////////////////////////////////////////////////////////////////
void
PDFView::SetRotation (float rotation)
{
	if (mRotation != rotation) {
		RecordHistory();
		gApp->GetSettings()->SetRotation(rotation);
		mRotation = rotation;
		PDFWindow* w = GetPDFWindow();
		if (w) w->SetRotation(mRotation);
		Redraw ();
	}
}

void
PDFView::RotateClockwise() {
	SetRotation(((int)mRotation + 90) % 360);
}

void
PDFView::RotateAntiClockwise() {
	SetRotation(((int)mRotation - 90 + 360) % 360);
}

///////////////////////////////////////////////////////////////////////////
// The marker and the tools of the toolbar

bool
PDFView::SelectingText() const
{
	return mMarkupArmed || SelectModifierDown();
}


int
PDFView::ArmedState() const
{
	if (mMarkupArmed)
		return mArmedNote ? 2 : 1;
	if (mTool == kToolNote || mTool == kToolFreeText)
		return 2;
	return mTool != kToolNone ? 3 : 0;
}


void
PDFView::ToolsChanged()
{
	if (PDFWindow* window = GetPDFWindow())
		window->ToolsChanged();
}


void
PDFView::ArmMarkup(MarkupType type, uint32 rgb, bool note)
{
	if (mDoc == NULL || !mDoc->CanMarkText() || !mDoc->CanCopy()) {
		beep();
		return;
	}
	// some text that is selected is marked at once
	if (HasTextSelection()) {
		MarkSelection(type, rgb, note);
		return;
	}
	if (mTool != kToolNone)
		CancelTool();
	mMarkupArmed = true;
	mArmedNote = note;
	mArmedType = type;
	mArmedColor = rgb;
	SetViewCursor(gApp->textSelectionCursor);
	ToolsChanged();
}


void
PDFView::DisarmMarkup()
{
	if (!mMarkupArmed)
		return;
	mMarkupArmed = false;
	mArmedNote = false;
	SetViewCursor(gApp->handCursor);
	ToolsChanged();
}


// the text that was selected with the marker armed is marked, and the marker is put down
void
PDFView::ApplyArmedMarkup()
{
	MarkupType type = mArmedType;
	uint32 rgb = mArmedColor;
	bool note = mArmedNote;
	DisarmMarkup();
	MarkSelection(type, rgb, note);
}


// marks the selected text, and asks for the note if it is a margin note
void
PDFView::MarkSelection(MarkupType type, uint32 rgb, bool note)
{
	int page = std::min(mInteractionPage, TextEndPage());
	if (!AnnotateSelection(type, rgb))
		return;
	if (note)
		EditNewestNote(page);
}


// opens the note of the mark that was added last on the page
void
PDFView::EditNewestNote(int page)
{
	std::vector<DocAnnotationEntry> entries;
	if (mDoc == NULL || !mDoc->ListAnnotationsOnPage(page, entries))
		return;
	int newest = -1;
	for (size_t i = 0; i < entries.size(); i++) {
		if (entries[i].annotation.isMarkup && !entries[i].annotation.continued && entries[i].annotation.index > newest)
			newest = entries[i].annotation.index;
	}
	if (newest < 0)
		return;
	BMessage edit(EDIT_NOTE_MSG);
	edit.AddInt32("page", page);
	edit.AddInt32("index", newest);
	edit.AddString("text", "");
	if (Window() != NULL)
		Window()->PostMessage(&edit, this);
}


BString
PDFView::LinkToHere()
{
	DeepLink::Place place;
	if (mDoc == NULL)
		return BString();
	// in a book the pages change with the text size, the place is told by the text
	place.page = mDoc->IsReflowable() ? 0 : ActivePage();
	if (const DocAnnotation* annotation = SelectedAnnotation()) {
		place.annotation = annotation->id;
	} else if (HasTextSelection()) {
		if (BString* text = GetSelectedText()) {
			text->ReplaceAll("\r", " ");
			text->ReplaceAll("\n", " ");
			while (text->FindFirst("  ") >= 0)
				text->ReplaceAll("  ", " ");
			text->Trim();
			if (text->Length() > 150)
				text->Truncate(150);
			place.quote = *text;
			delete text;
		}
	} else if (mDoc->IsReflowable()) {
		TextAnchor anchor;
		if (mDoc->MakeAnchor(ActivePage(), &anchor)) {
			place.cfi = anchor.cfi;
			if (place.cfi.IsEmpty())
				place.quote = anchor.quote;
		}
	}
	return DeepLink::Make(mDoc->Path(), place);
}


void
PDFView::CopyPlaceLink()
{
	BString link = LinkToHere();
	if (link.Length() > 0)
		CopyText(&link);
}


void
PDFView::MarginNoteOnSelection()
{
	if (!HasTextSelection() || mDoc == NULL || !mDoc->CanMarkText()) {
		beep();
		return;
	}
	MarkSelection(kMarkupHighlight, 0xffeb3b, true);
}


// The Note button: puts the tool down if it is armed, else opens its menu (a margin note, a note on the page, a text).
void
PDFView::NoteButton(BPoint screenPoint)
{
	if (mDoc == NULL)
		return;
	if (ArmedState() == 2) {
		if (mMarkupArmed)
			DisarmMarkup();
		else
			CancelTool();
		return;
	}
	ShowNoteMenu(screenPoint);
}


void
PDFView::ShowNoteMenu(BPoint screenPoint)
{
	BPopUpMenu* menu = new BPopUpMenu("NoteMenu", false, false);
	menu->SetAsyncAutoDestruct(true);

	BMenuItem* item = new BMenuItem(B_TRANSLATE("Margin note"), new BMessage(NOTE_MARGIN_MSG));
	item->SetTarget(this);
	item->SetEnabled(mDoc->CanMarkText() && mDoc->CanCopy());
	menu->AddItem(item);
	menu->AddSeparatorItem();
	static const struct { const char* label; PlacementTool tool; } kNotes[] = {
		{ B_TRANSLATE_MARK("Note on the page"), kToolNote }, { B_TRANSLATE_MARK("Text on the page"), kToolFreeText }
	};
	for (size_t i = 0; i < sizeof(kNotes) / sizeof(kNotes[0]); i++) {
		BMessage* message = new BMessage(ADD_TOOL_MSG);
		message->AddInt32("tool", kNotes[i].tool);
		item = new BMenuItem(B_TRANSLATE_NOCOLLECT(kNotes[i].label), message);
		item->SetTarget(this);
		item->SetEnabled(mDoc->CanDrawAnnotations());
		menu->AddItem(item);
	}
	menu->Go(screenPoint, true, true, true);
}


bool
PDFView::MarginNotesShown() const
{
	return gApp->GetSettings()->GetShowMarginNotes();
}


void
PDFView::SetMarginNotesShown(bool shown)
{
	gApp->GetSettings()->SetShowMarginNotes(shown);
	Invalidate();
}


// ---- the fancy mode: a page that turns

bool
PDFView::FancyMode() const
{
	return gApp->GetSettings()->GetFancyMode();
}


void
PDFView::SetFancyMode(bool fancy)
{
	gApp->GetSettings()->SetFancyMode(fancy);
	gApp->SaveSettings();
	if (!fancy)
		CancelTurn();
	else
		PrepareTurnSound();
}


// A step of one page or spread in a flow of pages (not in the continuous flow, where the view scrolls, and not for a webtoon, nor
// for a jump of many pages) turns the page, if the mode is on.
bool
PDFView::TurnWanted(int page)
{
	if (!(FancyMode() || mTurnForced) || mDoc == NULL || Window() == NULL || mLayingOut || mLayout.IsContinuous()
		|| mLayout.TopToBottom() || mSlots.empty())
		return false;
	int target = mLayout.NormalizePage(page);
	if (target == mCurrentPage)
		return false;
	return target == mLayout.NextSpreadPage(mCurrentPage) || target == mLayout.PreviousSpreadPage(mCurrentPage);
}


// What the view shows: the pages as they are drawn, around the color of the desktop, in a bitmap of the size of the view.
BBitmap*
PDFView::Snapshot(BRect* area, int* pages)
{
	BRect bounds(Bounds());
	BBitmap* bitmap = new BBitmap(BRect(0, 0, bounds.Width(), bounds.Height()), B_RGB32, true);
	BView* view = new BView(bitmap->Bounds(), "snapshot", B_FOLLOW_NONE, B_WILL_DRAW);
	bitmap->AddChild(view);
	bitmap->Lock();
	view->SetHighColor(DesktopColor());
	view->FillRect(view->Bounds());
	*pages = 0;
	*area = BRect();
	for (size_t i = 0; i < mSlots.size(); i++) {
		const PageSlot* slot = mSlots[i];
		BBitmap* page = slot->page->GetBitmap();
		if (page == NULL)
			continue;
		BPoint at = slot->origin - bounds.LeftTop();
		view->DrawBitmap(page, BRect(0, 0, slot->page->GetWidth() - 1, slot->page->GetHeight() - 1),
			BRect(at.x, at.y, at.x + slot->page->GetWidth() - 1, at.y + slot->page->GetHeight() - 1));
		view->SetHighColor(ui_color(B_SHADOW_COLOR));
		view->StrokeRect(BRect(at.x - 1, at.y - 1, at.x + slot->page->GetWidth(), at.y + slot->page->GetHeight()));
		BRect rect(at.x, at.y, at.x + slot->page->GetWidth() - 1, at.y + slot->page->GetHeight() - 1);
		*area = area->IsValid() ? (*area | rect) : rect;
		(*pages)++;
	}
	view->Sync();
	bitmap->Unlock();
	bitmap->RemoveChild(view);
	delete view;
	if (area->IsValid())
		*area = *area & bitmap->Bounds();
	return bitmap;
}


bool
PDFView::SlotsReady() const
{
	if (mSlots.empty())
		return false;
	for (size_t i = 0; i < mSlots.size(); i++) {
		if (mSlots[i]->rendering || mSlots[i]->page->GetBitmap() == NULL)
			return false;
	}
	return true;
}


// The page is going to change: what is shown now is kept, and the turn starts when the page that comes has been drawn.
void
PDFView::BeginTurn(int page)
{
	CancelTurn();
	PrepareTurnSound();
	BRect fromArea;
	int fromPages = 0;
	mTurnFrom = Snapshot(&fromArea, &fromPages);
	mTurnForward = mLayout.NormalizePage(page) == mLayout.NextSpreadPage(mCurrentPage);
	mTurnGeometry.area = fromArea;
	mTurnGeometry.spread = fromPages > 1;
	mTurnGeometry.rightToLeft = mLayout.RightToLeft();
	mTurnState = kTurnWaiting;
	BMessage timeout(PAGE_TURN_TIMEOUT_MSG);
	mTurnRunner = new BMessageRunner(BMessenger(this), &timeout, 600000, 1);
}


// The page that comes is there: the turn goes from what was shown to what is shown, or backwards when the page goes back (a step
// back is the step forward the other way round).
void
PDFView::TurnReady()
{
	delete mTurnRunner;
	mTurnRunner = NULL;
	BRect toArea;
	int toPages = 0;
	mTurnTo = Snapshot(&toArea, &toPages);
	if (!toArea.IsValid() || !mTurnGeometry.area.IsValid()) {
		CancelTurn();
		return;
	}
	if (toPages > 1)
		mTurnGeometry.spread = true;
	mTurnGeometry.area = mTurnGeometry.area | toArea;
	BRect bounds(Bounds());
	mTurnFrame = new BBitmap(BRect(0, 0, bounds.Width(), bounds.Height()), B_RGB32, true);
	mTurnFrameView = new BView(mTurnFrame->Bounds(), "turn", B_FOLLOW_NONE, B_WILL_DRAW);
	mTurnFrame->AddChild(mTurnFrameView);
	mTurnState = kTurnRunning;
	if (mTurnFreeze < 0)
		PlayTurnSound();
	mTurnStart = system_time();	// (after the sound has been started)
	TurnTick();
}


void
PDFView::TurnTick()
{
	if (mTurnState != kTurnRunning || mTurnFrom == NULL || mTurnTo == NULL)
		return;
	float time = mTurnFreeze >= 0 ? mTurnFreeze : (float)(system_time() - mTurnStart) / (float)PageTurn::kDuration;
	if (time >= 1) {
		CancelTurn();	// done
		return;
	}
	float progress = PageTurn::Ease(time);
#ifdef TOJI_TESTING
	bigtime_t began = system_time();
#endif
	mTurnFrame->Lock();
	if (mTurnForward)
		PageTurn::Draw(mTurnFrameView, mTurnFrom, mTurnTo, mTurnGeometry, progress);
	else
		PageTurn::Draw(mTurnFrameView, mTurnTo, mTurnFrom, mTurnGeometry, 1 - progress);
	mTurnFrameView->Sync();
	mTurnFrame->Unlock();
	// the picture is shown at once (the messages for the next frames would be in front of the update otherwise, and the turn
	// would be over before it is seen), and there is at most one message for the next frame
	Invalidate();
	if (Window() != NULL)
		Window()->UpdateIfNeeded();
	if (mTurnFreeze < 0) {
		delete mTurnRunner;
		BMessage tick(PAGE_TURN_TICK_MSG);
		mTurnRunner = new BMessageRunner(BMessenger(this), &tick, 8000, 1);
	}
#ifdef TOJI_TESTING
	if (FILE* log = fopen("/tmp/ts_test.out", "a")) { fprintf(log, "turn frame t=%.2f took %d ms\n", time, (int)((system_time() - began) / 1000)); fclose(log); }
#endif
}


void
PDFView::DrawTurn()
{
#ifdef TOJI_TESTING
	if (FILE* log = fopen("/tmp/ts_test.out", "a")) { fprintf(log, "turn drawn at %d ms\n", (int)((system_time() - mTurnStart) / 1000)); fclose(log); }
#endif
	BPoint at = Bounds().LeftTop();
	SetDrawingMode(B_OP_COPY);
	if (mTurnState == kTurnRunning && mTurnFrame != NULL)
		DrawBitmap(mTurnFrame, at);
	else if (mTurnFrom != NULL)
		DrawBitmap(mTurnFrom, at);
}


// The turn is over, or is given up: the view shows the page as it is.
void
PDFView::CancelTurn()
{
	if (mTurnState == kTurnNone && mTurnFrom == NULL)
		return;
	delete mTurnRunner;
	mTurnRunner = NULL;
	mTurnState = kTurnNone;
	if (mTurnFrame != NULL && mTurnFrameView != NULL)
		mTurnFrame->RemoveChild(mTurnFrameView);
	delete mTurnFrameView;
	mTurnFrameView = NULL;
	delete mTurnFrame;
	mTurnFrame = NULL;
	delete mTurnFrom;
	mTurnFrom = NULL;
	delete mTurnTo;
	mTurnTo = NULL;
	if (Window() != NULL)
		Invalidate();
}


// the sound of a page, from the sounds next to the program; it is loaded before it is needed (that takes a while)
void
PDFView::PrepareTurnSound()
{
	if (mTurnSound != NULL || mTurnSoundMissing || !gApp->GetSettings()->GetFancySound())
		return;
	BPath path(*gApp->GetAppPath());
	path.Append("sounds/pageturn.wav");
	mTurnSound = new BSimpleGameSound(path.Path());
#ifdef TOJI_TESTING
	if (FILE* log = fopen("/tmp/ts_test.out", "a")) { fprintf(log, "turn sound %s: %s\n", path.Path(), strerror(mTurnSound->InitCheck())); fclose(log); }
#endif
	if (mTurnSound->InitCheck() != B_OK) {
		delete mTurnSound;
		mTurnSound = NULL;
		mTurnSoundMissing = true;
	}
}


void
PDFView::PlayTurnSound()
{
	if (!gApp->GetSettings()->GetFancySound())
		return;
	PrepareTurnSound();
	if (mTurnSound == NULL)
		return;
	mTurnSound->StopPlaying();
	mTurnSound->StartPlaying();
}


// ---- the notes in the margin: a small note at the right edge of the page for each mark that has a note

void
PDFView::MarginNoteBoxes(std::vector<MarginBox>* boxes)
{
	boxes->clear();
	if (!MarginNotesShown() || mPage == NULL)
		return;
	struct Item {
		float y;
		const DocAnnotation* annotation;
	};
	std::vector<Item> items;
	const std::vector<DocAnnotation>& list = mPage->mAnnotations;
	for (size_t i = 0; i < list.size(); i++) {
		const DocAnnotation& a = list[i];
		if (!a.isMarkup || a.continued || a.contents.Length() == 0 || a.quads.empty())
			continue;
		Item item = { mPage->PageToDev(a.quads[0]).top, &a };
		items.push_back(item);
	}
	// from the top to the bottom, each below the one before
	for (size_t i = 0; i < items.size(); i++) {
		for (size_t k = i + 1; k < items.size(); k++) {
			if (items[k].y < items[i].y)
				std::swap(items[i], items[k]);
		}
	}
	const float kSize = 18;
	// in the white margin at the right border of the page
	float left = mWidth - kSize - 8;
	float lastBottom = -1000;
	for (size_t i = 0; i < items.size(); i++) {
		float y = items[i].y > lastBottom + 2 ? items[i].y : lastBottom + 2;
		MarginBox box;
		box.box = BRect(left, y, left + kSize - 1, y + kSize - 4);
		box.annotation = items[i].annotation;
		boxes->push_back(box);
		lastBottom = box.box.bottom;
	}
}


const DocAnnotation*
PDFView::MarginNoteAt(BPoint point)
{
	std::vector<MarginBox> boxes;
	MarginNoteBoxes(&boxes);
	for (size_t i = 0; i < boxes.size(); i++) {
		if (boxes[i].box.InsetByCopy(-4, -4).Contains(point))
			return boxes[i].annotation;
	}
	return NULL;
}


const DocAnnotation*
PDFView::MarginNoteAtView(BPoint point, PageSlot** found)
{
	for (size_t i = 0; i < mSlots.size(); i++) {
		SlotScope scope(this, mSlots[i]);
		std::vector<MarginBox> boxes;
		MarginNoteBoxes(&boxes);
		for (size_t k = 0; k < boxes.size(); k++) {
			if (boxes[k].box.OffsetByCopy(mLeft, mTop).InsetByCopy(-4, -4).Contains(point)) {
				if (found != NULL)
					*found = mSlots[i];
				return boxes[k].annotation;
			}
		}
	}
	return NULL;
}


void
PDFView::DrawMarginNotes(BRect)
{
	std::vector<MarginBox> boxes;
	MarginNoteBoxes(&boxes);
	if (boxes.empty())
		return;
	rgb_color fill = { 255, 235, 100, 255 };
	rgb_color edge = { 150, 120, 20, 255 };
	for (size_t i = 0; i < boxes.size(); i++) {
		BRect box = boxes[i].box.OffsetByCopy(mLeft, mTop);
		bool hover = boxes[i].annotation == mMarginHover;
		// the pointer on a note shows what it belongs to: a dotted line from the end of the marked words
		if (hover && !boxes[i].annotation->quads.empty()) {
			BRect words = mPage->PageToDev(boxes[i].annotation->quads[0]).OffsetByCopy(mLeft, mTop);
			float y = floorf(words.bottom - words.Height() * 0.12f);	// under the baseline
			SetHighColor(edge);
			for (float x = words.right + 2; x < box.left - 1; x += 3)
				StrokeLine(BPoint(x, y), BPoint(x, y));
		}
		SetHighColor(hover ? tint_color(fill, B_LIGHTEN_1_TINT) : fill);
		FillRoundRect(box, 2, 2);
		SetHighColor(edge);
		StrokeRoundRect(box, 2, 2);
		// the lines of the note
		float inset = 4;
		for (int line = 0; line < 3; line++) {
			float y = box.top + 3 + line * 4;
			StrokeLine(BPoint(box.left + inset, y), BPoint(box.right - inset - (line == 2 ? 4 : 0), y));
		}
	}
}


void
PDFView::ShowMarkerMenu(BPoint screenPoint)
{
	BPopUpMenu* menu = new BPopUpMenu("MarkerMenu", false, false);
	menu->SetAsyncAutoDestruct(true);

	for (int c = 0; c < kMarkerColorCount; c++) {
		BMessage* message = new BMessage(ARM_MARKUP_MSG);
		message->AddInt32("type", kMarkupHighlight);
		message->AddInt32("color", kMarkerColors[c].rgb);
		BMenuItem* item = new ColorMenuItem(B_TRANSLATE_NOCOLLECT(kMarkerColors[c].name), kMarkerColors[c].rgb, message);
		item->SetTarget(this);
		menu->AddItem(item);
	}
	menu->AddSeparatorItem();
	BMessage* underline = new BMessage(ARM_MARKUP_MSG);
	underline->AddInt32("type", kMarkupUnderline);
	underline->AddInt32("color", kMarkerColors[5].rgb);
	BMenuItem* item = new BMenuItem(B_TRANSLATE("Underline"), underline);
	item->SetTarget(this);
	menu->AddItem(item);
	BMessage* strike = new BMessage(ARM_MARKUP_MSG);
	strike->AddInt32("type", kMarkupStrikeOut);
	strike->AddInt32("color", kMarkerColors[5].rgb);
	item = new BMenuItem(B_TRANSLATE("Strike out"), strike);
	item->SetTarget(this);
	menu->AddItem(item);
	menu->Go(screenPoint, true, true, true);
}


void
PDFView::ShowShapesMenu(BPoint screenPoint)
{
	static const struct { const char* label; PlacementTool tool; } kShapes[] = {
		{ B_TRANSLATE_MARK("Rectangle"), kToolRectangle },
		{ B_TRANSLATE_MARK("Ellipse"), kToolEllipse }, { B_TRANSLATE_MARK("Line"), kToolLine },
		{ B_TRANSLATE_MARK("Arrow"), kToolArrow }, { B_TRANSLATE_MARK("Drawing"), kToolInk }
	};
	BPopUpMenu* menu = new BPopUpMenu("ShapesMenu", false, false);
	menu->SetAsyncAutoDestruct(true);
	for (size_t i = 0; i < sizeof(kShapes) / sizeof(kShapes[0]); i++) {
		BMessage* message = new BMessage(ADD_TOOL_MSG);
		message->AddInt32("tool", kShapes[i].tool);
		BMenuItem* item = new BMenuItem(B_TRANSLATE_NOCOLLECT(kShapes[i].label), message);
		item->SetTarget(this);
		menu->AddItem(item);
	}
	menu->Go(screenPoint, true, true, true);
}


///////////////////////////////////////////////////////////////////////////
// Text selection

void
PDFView::ClearQuads() {
	mQuads.clear();
	mPageQuads.clear();
	mTextEndPage = 0;
	mSpansPages = false;
}

// Whether the selection of text covers the page, and the part of it that is on the page: from the point where it began to
// the end of the page, the whole page, or from the beginning of the page to the end point.
bool
PDFView::SelectionOnPage(int page, fz_point* from, fz_point* to) {
	int first = mInteractionPage, last = TextEndPage();
	fz_point a = mTextStart, b = mTextEnd;
	if (last < first) {
		std::swap(first, last);
		std::swap(a, b);
	}
	if (page < first || page > last)
		return false;
	fz_rect bounds = fz_empty_rect;
	if (first != last && !mDoc->PageBounds(page, &bounds))
		return false;
	*from = page == first ? a : fz_make_point(bounds.x0, bounds.y0);
	*to = page == last ? b : fz_make_point(bounds.x1, bounds.y1);
	return true;
}

// the areas of the selection on a page that is shown (not the first one, whose areas are mQuads); NULL if it is not selected
const std::vector<fz_quad>*
PDFView::QuadsOnPage(int page) {
	if (page == mInteractionPage)
		return &mQuads;
	std::map<int, std::vector<fz_quad> >::iterator found = mPageQuads.find(page);
	if (found != mPageQuads.end())
		return &found->second;
	fz_point from, to;
	PageSlot* slot = SlotForPage(page);
	if (slot == NULL || !SelectionOnPage(page, &from, &to))
		return NULL;
	std::vector<fz_quad> quads;
	fz_stext_page* text = slot->page->Text();
	if (text != NULL) {
		quads.resize(kMaxQuads);
		int count = 0;
		DocumentLocker locker(mDoc);
		fz_context* context = mDoc->Context();
		fz_var(count);
		fz_try(context) {
			count = fz_highlight_selection(context, text, from, to, &quads[0], kMaxQuads);
		}
		fz_catch(context) {
			count = 0;
		}
		quads.resize(count);
	}
	return &(mPageQuads[page] = quads);
}

// the area that is selected, in coordinates of the bitmap
BRect
PDFView::SelectionBounds() {
	if (mSelectionKind == kSelectArea)
		return mSelection;

	BRect bounds;
	for (size_t i = 0; i < mQuads.size(); i++)
		bounds = bounds | mPage->PageToDev(mQuads[i]);
	return bounds;
}

///////////////////////////////////////////////////////////////////////////
bool
PDFView::InSelection(BPoint point) {
	BPoint p = CorrectMousePos(point);
	if (mSelectionKind == kSelectArea)
		return mSelection.Contains(p);
	return InTextSelection(p);
}

bool
PDFView::InTextSelection(BPoint point) {
	fz_point p = mPage->DevToPage(point);
	for (size_t i = 0; i < mQuads.size(); i++) {
		if (fz_is_point_inside_quad(p, mQuads[i]))
			return true;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::StartTextSelection(BPoint point) {
	mSelectionKind = kSelectText;
	ClearQuads();
	mTextStart = mTextEnd = mPage->DevToPage(point);
}

void
PDFView::ExtendTextSelection(BPoint point) {
	mTextEnd = mPage->DevToPage(LimitToPage(point));
	mTextEndPage = 0;
	UpdateQuads(true);
}

// The selection goes on to the point in the view: on the page that is there, or on the nearest page if the point is beside
// or between pages.
void
PDFView::ExtendTextSelectionTo(BPoint viewPoint) {
	PageSlot* slot = SlotAt(viewPoint);
	if (slot == NULL) {
		float best = 1e30f;
		for (size_t i = 0; i < mSlots.size(); i++) {
			PageSlot* candidate = mSlots[i];
			BRect area(candidate->origin.x, candidate->origin.y, candidate->origin.x + candidate->page->GetWidth() - 1,
				candidate->origin.y + candidate->page->GetHeight() - 1);
			float dx = viewPoint.x < area.left ? area.left - viewPoint.x : viewPoint.x > area.right ? viewPoint.x - area.right : 0;
			float dy = viewPoint.y < area.top ? area.top - viewPoint.y : viewPoint.y > area.bottom ? viewPoint.y - area.bottom : 0;
			// (the pages are read from the top to the bottom, so a vertical distance counts for more)
			float distance = dx * dx + dy * dy * 4;
			if (distance < best) {
				best = distance;
				slot = candidate;
			}
		}
	}
	if (slot == NULL)
		return;

	BPoint local = viewPoint - slot->origin;
	if (local.x < 0) local.x = 0;
	else if (local.x > slot->page->GetWidth() - 1) local.x = slot->page->GetWidth() - 1;
	if (local.y < 0) local.y = 0;
	else if (local.y > slot->page->GetHeight() - 1) local.y = slot->page->GetHeight() - 1;

	mTextEnd = slot->page->DevToPage(local);
	mTextEndPage = slot->number == mInteractionPage ? 0 : slot->number;
	UpdateQuads(true);
}

// Finds the areas of the text between the two points, in page space.
void
PDFView::UpdateQuads(bool invalidate) {
	BRect old;
	const bool wasSpanning = mSpansPages;
	if (invalidate && mSelectionKind == kSelectText)
		old = SelectionBounds();

	std::vector<fz_quad> quads;
	fz_stext_page* text = mPage->Text();
	fz_point from, to;
	if (text != NULL && SelectionOnPage(mInteractionPage, &from, &to)) {
		quads.resize(kMaxQuads);
		fz_quad* buffer = &quads[0];
		int count = 0;

		DocumentLocker locker(mDoc);
		fz_context* context = mDoc->Context();
		fz_var(count);
		fz_try(context) {
			count = fz_highlight_selection(context, text, from, to, buffer, kMaxQuads);
		}
		fz_catch(context) {
			count = 0;
		}
		quads.resize(count);
	}
	mQuads.swap(quads);
	mPageQuads.clear();
	mSpansPages = TextEndPage() != mInteractionPage;

	if (invalidate) {
		if (mSpansPages || wasSpanning) {
			Invalidate();	// (the other pages are not where the bounds of the first are)
		} else {
			BRect changed = old | SelectionBounds();
			if (changed.IsValid())
				Invalidate(changed.InsetByCopy(-2, -2).OffsetByCopy(mLeft, mTop));
		}
	}
}

// Selects the word or line at the position. Returns false if there is no text.
bool
PDFView::SelectTextAt(BPoint point, int mode) {
	fz_stext_page* text = mPage->Text();
	if (text == NULL)
		return false;

	fz_point start = mPage->DevToPage(CorrectMousePos(point));
	fz_point end = start;
	{
		DocumentLocker locker(mDoc);
		fz_context* context = mDoc->Context();
		fz_try(context) {
			fz_snap_selection(context, text, &start, &end, mode);
		}
		fz_catch(context) {
			return false;
		}
	}

	BRect old = mSelected != NOT_SELECTED ? SelectionBounds() : BRect();
	mSelectionKind = kSelectText;
	mTextStart = start;
	mTextEnd = end;
	mTextEndPage = 0;
	UpdateQuads(false);
	if (mQuads.empty()) {
		mSelected = NOT_SELECTED;
		return false;
	}

	mSelected = SELECTED;
	BRect changed = old | SelectionBounds();
	if (changed.IsValid())
		Invalidate(changed.InsetByCopy(-2, -2).OffsetByCopy(mLeft, mTop));
	return true;
}

///////////////////////////////////////////////////////////////////////////
// Selects the text that has been found, from the start point to the end point (page space) and makes it
// visible. The caller holds the lock of the window.
void
PDFView::SelectFound(fz_point start, fz_point end) {
	BRect old = mSelected != NOT_SELECTED ? SelectionBounds() : BRect();
	mSelectionKind = kSelectText;
	mTextStart = start;
	mTextEnd = end;
	mTextEndPage = 0;
	UpdateQuads(false);
	mSelected = mQuads.empty() ? NOT_SELECTED : SELECTED;

	BRect selection = SelectionBounds();
	BRect changed = old | selection;
	if (changed.IsValid())
		Invalidate(changed.InsetByCopy(-2, -2).OffsetByCopy(mLeft, mTop));

	if (selection.IsValid()) {
		// make selection visible
		BRect bounds(Bounds());
		BRect shown = selection.OffsetByCopy(mLeft, mTop);
		float x = bounds.left, y = bounds.top;
		if (shown.left < bounds.left || shown.right > bounds.right)
			x = selection.left - 20;
		if (shown.top < bounds.top || shown.bottom > bounds.bottom)
			y = selection.top - 40;
		ScrollTo(x, y);
	}
	SelectionChanged();
}

///////////////////////////////////////////////////////////////////////////
// returns the selected text, the caller deletes the string
BString*
PDFView::GetSelectedText() {
	if (mSelected != SELECTED)
		return NULL;

	// a selection over several pages: the text of each page, one after the other
	if (mSelectionKind == kSelectText && TextEndPage() != mInteractionPage) {
		int first = std::min(mInteractionPage, TextEndPage()), last = std::max(mInteractionPage, TextEndPage());
		BString* all = new BString();
		for (int page = first; page <= last; page++) {
			fz_point from, to;
			BString part;
			if (SelectionOnPage(page, &from, &to) && mDoc->SelectionText(page, from, to, &part) && part.Length() > 0) {
				if (all->Length() > 0)
					all->Append("\n");
				all->Append(part);
			}
		}
		if (all->Length() == 0) {
			delete all;
			return NULL;
		}
		return all;
	}

	fz_stext_page* text = mPage->Text();
	if (text == NULL)
		return NULL;

	char* copied = NULL;
	DocumentLocker locker(mDoc);
	fz_context* context = mDoc->Context();

	fz_var(copied);
	fz_try(context) {
		if (mSelectionKind == kSelectText)
			copied = fz_copy_selection(context, text, mTextStart, mTextEnd, 0);
		else {
			fz_point a = mPage->DevToPage(mSelection.LeftTop());
			fz_point b = mPage->DevToPage(mSelection.RightBottom());
			fz_rect area = fz_make_rect(fminf(a.x, b.x), fminf(a.y, b.y), fmaxf(a.x, b.x), fmaxf(a.y, b.y));
			copied = fz_copy_rectangle(context, text, area, 0);
		}
	}
	fz_catch(context) {
		copied = NULL;
	}

	if (copied == NULL)
		return NULL;

	BString* result = new BString(copied);
	fz_free(context, copied);
	if (result->Length() == 0) {
		delete result;
		return NULL;
	}
	return result;
}

///////////////////////////////////////////////////////////////////////////
void
PDFView::CopyText(BString *str) {
	if (be_clipboard->Lock()) {
		be_clipboard->Clear();

		BMessage *clip = NULL;
		if ((clip = be_clipboard->Data()) != NULL) {
			// copy text to clipboard
			clip->AddData("text/plain", B_MIME_TYPE, str->String(), str->Length());
			be_clipboard->Commit();
		}
		be_clipboard->Unlock();
	}
}

///////////////////////////////////////////////////////////////////////////
// the bitmap of the area that is selected; the caller deletes it
static BBitmap*
CopyBitmapArea(BBitmap* source, BRect area, float width, float height) {
	BRect sel(max_c(area.left, 0), max_c(area.top, 0), min_c(area.right, width - 1),
		min_c(area.bottom, height - 1));
	if (!sel.IsValid())
		return NULL;

	BRect rect(0, 0, sel.Width(), sel.Height());
	BView view(rect, NULL, B_FOLLOW_NONE, B_WILL_DRAW);
	BBitmap* bitmap = new BBitmap(rect, source->ColorSpace(), true);
	if (bitmap->Lock()) {
		bitmap->AddChild(&view);
		view.DrawBitmap(source, sel, rect);
		view.Sync();
		bitmap->RemoveChild(&view);
		bitmap->Unlock();
	}
	return bitmap;
}

void
PDFView::CopySelection() {
	if (mSelected != SELECTED)
		return;

	BString* text = GetSelectedText();
	if (mSelectionKind == kSelectText) {
		// text only, a flowing selection has no rectangle to take a picture of
		if (text != NULL) {
			CopyText(text);
			delete text;
		}
		return;
	}

	if (mSelection.left < mSelection.right && mSelection.top < mSelection.bottom) {
		if (be_clipboard->Lock()) {
			be_clipboard->Clear();

			BMessage *clip = NULL;
			if ((clip = be_clipboard->Data()) != NULL) {
				// copy bitmap to clipboard
				BBitmap* bitmap = CopyBitmapArea(mBitmap, mSelection, mWidth, mHeight);
				if (bitmap != NULL) {
					BMessage data;
					bitmap->Archive(&data);
					clip->AddMessage("image/x-vnd.Be-bitmap", &data);
					clip->AddRect("rect", bitmap->Bounds());
					delete bitmap;
				}

				// copy text to clipboard
				if (text != NULL) {
					clip->AddData("text/plain", B_MIME_TYPE, text->String(), text->Length());
				}
				be_clipboard->Commit();
			}
			be_clipboard->Unlock();
		}
	}
	delete text;
}


///////////////////////////////////////////////////////////
void PDFView::SelectAll() {
	if (!mDoc->CanCopy())
		return;

	fz_rect bounds;
	if (!mDoc->PageBounds(mCurrentPage, &bounds))
		return;

	mSelectionKind = kSelectText;
	mTextStart = fz_make_point(bounds.x0, bounds.y0);
	mTextEnd = fz_make_point(bounds.x1, bounds.y1);
	mTextEndPage = 0;
	UpdateQuads(false);
	mSelected = mQuads.empty() ? NOT_SELECTED : SELECTED;
	SelectionChanged();
	Invalidate();
}

///////////////////////////////////////////////////////////
void PDFView::SelectNone() {
	if (mSelected == SELECTED) {
		mSelected = NOT_SELECTED;
		ClearQuads();
		SelectionChanged();
		Invalidate();
	}
}

///////////////////////////////////////////////////////////
void PDFView::SetFilledSelection(bool filled) {
	mFilledSelection = filled;
	gApp->GetSettings()->SetFilledSelection(filled);

	if (mSelected == SELECTED) {
		Invalidate();
	}
}

///////////////////////////////////////////////////////////
// Marks the selected text in the document: adds an annotation that covers the lines of the selection.
bool
PDFView::AnnotateSelection(MarkupType type, uint32 rgb)
{
	const bool spanning = TextEndPage() != mInteractionPage;
	if (!HasTextSelection() || (mQuads.empty() && !spanning) || !mDoc->CanMarkText()) {
		beep();
		return false;
	}

	if (!ConfirmEditable())
		return false;

	float color[3] = { ((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f, (rgb & 0xff) / 255.0f };
	TimingStart();
	WaitForPage(true);
	TimingMark("render aborted");
	if (spanning) {
		// a mark on each page that the selection covers
		int first = std::min(mInteractionPage, TextEndPage()), last = std::max(mInteractionPage, TextEndPage());
		std::vector<int> marked;
		for (int page = first; page <= last; page++) {
			fz_point from, to;
			std::vector<fz_quad> quads;
			if (!SelectionOnPage(page, &from, &to) || !mDoc->SelectionQuads(page, from, to, &quads))
				continue;
			if (mDoc->AddMarkup(page, type, &quads[0], (int)quads.size(), color))
				marked.push_back(page);
		}
		if (marked.empty())
			return false;
		SelectNone();
		for (size_t i = 0; i < marked.size(); i++)
			AnnotationsChanged(marked[i]);
		return true;
	}
	std::vector<fz_quad> quads = mQuads;
	if (!mDoc->AddMarkup(ActivePage(), type, &quads[0], (int)quads.size(), color))
		return false;
	TimingMark("annotation added to the document");

	SelectNone();
	AnnotationsChanged(ActivePage());
	TimingMark("view updated (render started)");
	return true;
}


// A file that cannot be written (system directory, read-only volume) can still be annotated, but the changes can
// only be kept in a copy. Says so before the first change.
bool
PDFView::ConfirmEditable()
{
	if (mDoc->IsWritable() || mReadOnlyWarned)
		return true;
#ifdef TOJI_TESTING
	if (getenv("TOJI_AUTOCONFIRM") != NULL)
		return true;
#endif

	BAlert* alert = new BAlert("Read-only", B_TRANSLATE("This file is read-only. You can add annotations, "
		"but to keep them you have to save a copy with “Save as…”."), B_TRANSLATE("Cancel"),
		B_TRANSLATE("Continue"), NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	if (alert->Go() != 1)
		return false;
	mReadOnlyWarned = true;
	return true;
}


// Shows the changed annotations of the page.
void
PDFView::AnnotationsChanged(int page)
{
	// the page is new, nothing of the old one stays; but a selected annotation is still the one (callers that
	// remove it have cleared the selection)
	int selected = mAnnotationIndex;
	mNoteTip = 0;
	PageSlot* slot = page > 0 ? SlotForPage(page) : NULL;
	if (mDoc->IsReflowable() && (page <= 0 || PageShown(page))) {
		// a mark can run over a page break: every page that is shown is drawn again, with the old image kept
		for (size_t i = 0; i < mSlots.size(); i++) {
			mSlots[i]->renderer->Abort();
			mSlots[i]->renderer->Wait();
			StartRender(mSlots[i], true);
			if (mSlots[i] == mActive)
				mRendering = true;
		}
	} else if (slot != NULL) {
		// only this page is rendered again, and the old image stays until the new one is there
		slot->renderer->Abort();
		slot->renderer->Wait();
		StartRender(slot, true);
		if (slot == mActive)
			mRendering = true;
	} else {
		mRenderedPage = 0;
		if (page > 0 && !PageShown(page))
			SetPage(page);	// draws it
		else
			Redraw();
		mAnnotationIndex = selected;
	}
	SelectionChanged();
	if (PDFWindow* window = GetPDFWindow()) {
		// the parts of a mark on other pages change with it: the list is read again as a whole
		window->AnnotationsChanged(mDoc->IsReflowable() ? 0 : page);
	}
}


// The text size of a book: the pages are made again for it and the reader stays where the text was.
void
PDFView::ChangeTextSize(bool larger)
{
	if (mDoc == NULL || !mDoc->IsReflowable())
		return;

	static const float kSizes[] = { 8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 32, 40 };
	const int count = sizeof(kSizes) / sizeof(kSizes[0]);
	float current = mDoc->TextSize();
	float size = current;
	if (larger) {
		for (int i = 0; i < count; i++) {
			if (kSizes[i] > current + 0.01f) {
				size = kSizes[i];
				break;
			}
		}
	} else {
		for (int i = count - 1; i >= 0; i--) {
			if (kSizes[i] < current - 0.01f) {
				size = kSizes[i];
				break;
			}
		}
	}
	if (size == current || mLayingOut)
		return;

	TimingStart();
	WaitForPage(true);
	SelectNone();
	mAnnotationIndex = -1;

	mLayingOut = true;
	mLayoutSize = size;
	mLayoutFromPage = mCurrentPage;
	// the busy window only comes if the layout is not done after a moment
	BMessage show(SHOW_BUSY_MSG);
	mBusyRunner = new BMessageRunner(BMessenger(this), &show, 400000, 1);
	mLayoutThread = spawn_thread(LayoutThread, "layout", B_NORMAL_PRIORITY, this);
	if (mLayoutThread < 0) {
		mLayoutThread = -1;
		mLayingOut = false;
		StopBusy();
		return;
	}
	resume_thread(mLayoutThread);
}


int32
PDFView::LayoutThread(void* data)
{
	PDFView* view = (PDFView*)data;
	view->mLayoutToPage = view->mDoc->ChangeTextSize(view->mLayoutSize, view->mLayoutFromPage);
	BMessenger(view).SendMessage(LAYOUT_DONE_MSG);
	return 0;
}


void
PDFView::StopBusy()
{
	delete mBusyRunner;
	mBusyRunner = NULL;
	if (mBusyWindow != NULL) {
		mBusyWindow->Disappear();
		if (mBusyWindow->Lock())
			mBusyWindow->Quit();
		mBusyWindow = NULL;
	}
}


void
PDFView::WaitForLayout()
{
	if (mLayoutThread >= 0) {
		// the layout is given up (the chapter that is laid out is finished first), not waited for to its end
		if (mDoc != NULL)
			mDoc->AbortLayout();
		status_t result;
		wait_for_thread(mLayoutThread, &result);
		mLayoutThread = -1;
	}
	mLayingOut = false;
}


// The pages are made for the new text size: what is shown is made anew.
void
PDFView::FinishTextSize()
{
	WaitForLayout();
	StopBusy();
	TimingMark("text size: laid out");

	gApp->GetSettings()->SetTextSize(mDoc->TextSize());
	mCurrentPage = mLayoutToPage;
	mInteractionPage = mLayoutToPage;
	mRenderedPage = 0;
	mFindHighlight = false;
	Redraw();
	if (mLayout.IsContinuous())
		ScrollToPage(mCurrentPage, true);
	if (PDFWindow* window = GetPDFWindow())
		window->TextSizeChanged();
}


void
PDFView::Undo()
{
	WaitForPage(true);
	mAnnotationIndex = -1;
	int page = mDoc->Undo();
	if (page > 0)
		AnnotationsChanged(page);
}


// the color of what is drawn, for marks of the same kind as the red underline
static const uint32 kDrawingColor = 0xe53935;


void
PDFView::SetTool(PlacementTool tool, const fz_point* position)
{
	if (tool == kToolNone || !mDoc->CanDrawAnnotations()) {
		beep();
		return;
	}

	if ((tool == kToolNote || tool == kToolFreeText) && position != NULL) {
		// where the menu was opened
		AskForText(tool, *position);
		return;
	}

	if (mMarkupArmed)
		DisarmMarkup();
	mTool = tool;
	SetViewCursor(mToolCursor);
	ToolsChanged();
}


void
PDFView::CancelTool()
{
	BRect old = ToolBounds();
	mTool = kToolNone;
	mToolPoints.clear();
	if (mMouseAction == TOOL_ACTION)
		SetAction(NO_ACTION);
	if (old.IsValid())
		Invalidate(old.InsetByCopy(-4, -4).OffsetByCopy(mLeft, mTop));
	SetViewCursor(gApp->handCursor);
	ToolsChanged();
}


void
PDFView::BeginTool(BPoint point)
{
	BPoint p = CorrectMousePos(point);
	if (p.x < 0 || p.y < 0 || p.x >= mWidth || p.y >= mHeight)
		return;

	// (a note or a text is asked for when the button is released, see FinishTool: a window that opens under the pressed
	// button is a surprise)
	mToolStart = mToolEnd = p;
	mToolPoints.clear();
	mToolPoints.push_back(p);
	SetAction(TOOL_ACTION);
	SetMouseEventMask(B_POINTER_EVENTS);
}


// The drag is over: creates what was drawn, if it is big enough to be meant. Otherwise the tool stays.
void
PDFView::FinishTool(BPoint)
{
	if (mTool == kToolNote || mTool == kToolFreeText) {
		// a click is all it takes, the text is asked for
		PlacementTool tool = mTool;
		BPoint at = mToolStart;
		CancelTool();
		AskForText(tool, mPage->DevToPage(at));
		return;
	}

	BRect bounds = ToolBounds();
	float dx = mToolEnd.x - mToolStart.x, dy = mToolEnd.y - mToolStart.y;
	float length = sqrtf(dx * dx + dy * dy);
	bool big = false;
	switch (mTool) {
		case kToolRectangle:
		case kToolEllipse:
			big = fabsf(dx) >= 4 && fabsf(dy) >= 4;
			break;
		case kToolLine:
		case kToolArrow:
			big = length >= 6;
			break;
		case kToolInk:
			big = mToolPoints.size() >= 3 || (mToolPoints.size() == 2 && length >= 6);
			break;
		default:
			break;
	}

	if (!big) {
		mToolPoints.clear();
		if (bounds.IsValid())
			Invalidate(bounds.InsetByCopy(-4, -4).OffsetByCopy(mLeft, mTop));
		return;
	}

	PlacementTool tool = mTool;
	fz_point from = mPage->DevToPage(mToolStart), to = mPage->DevToPage(mToolEnd);
	std::vector<fz_point> path;
	for (size_t i = 0; i < mToolPoints.size(); i++)
		path.push_back(mPage->DevToPage(mToolPoints[i]));
	CancelTool();

	if (!ConfirmEditable())
		return;

	WaitForPage(true);
	bool ok = false;
	switch (tool) {
		case kToolRectangle:
			ok = mDoc->AddShape(ActivePage(), kShapeRectangle, from, to, kDrawingColor);
			break;
		case kToolEllipse:
			ok = mDoc->AddShape(ActivePage(), kShapeEllipse, from, to, kDrawingColor);
			break;
		case kToolLine:
			ok = mDoc->AddShape(ActivePage(), kShapeLine, from, to, kDrawingColor);
			break;
		case kToolArrow:
			ok = mDoc->AddShape(ActivePage(), kShapeArrow, from, to, kDrawingColor);
			break;
		case kToolInk:
			ok = mDoc->AddInk(ActivePage(), &path[0], (int)path.size(), kDrawingColor);
			break;
		default:
			break;
	}
	if (ok)
		AnnotationsChanged(ActivePage());
	else
		Redraw();
}


// Asks for the text of a note or of text on the page; the annotation is made when it comes back.
void
PDFView::AskForText(PlacementTool tool, fz_point position)
{
	if (!ConfirmEditable())
		return;

	BMessage message(CREATE_TEXT_MSG);
	message.AddInt32("tool", tool);
	message.AddInt32("page", ActivePage());
	message.AddFloat("x", position.x);
	message.AddFloat("y", position.y);
	new NoteWindow(Window(), BMessenger(this), message, "");
}


// the area in the bitmap that the drawing in progress covers
BRect
PDFView::ToolBounds() const
{
	if (mTool == kToolNone || mMouseAction != TOOL_ACTION)
		return BRect();

	BRect bounds(mToolStart, mToolStart);
	bounds = bounds | BRect(mToolEnd, mToolEnd);
	for (size_t i = 0; i < mToolPoints.size(); i++)
		bounds = bounds | BRect(mToolPoints[i], mToolPoints[i]);
	return bounds;
}


void
PDFView::DrawToolPreview()
{
	if (mTool == kToolNone || mMouseAction != TOOL_ACTION)
		return;

	rgb_color color = { (uint8)(kDrawingColor >> 16), (uint8)(kDrawingColor >> 8), (uint8)kDrawingColor, 255 };
	SetHighColor(color);
	SetPenSize(2);
	BPoint offset(mLeft, mTop);
	BPoint a = mToolStart + offset, b = mToolEnd + offset;
	BRect box(min_c(a.x, b.x), min_c(a.y, b.y), max_c(a.x, b.x), max_c(a.y, b.y));
	switch (mTool) {
		case kToolRectangle:
			StrokeRect(box);
			break;
		case kToolEllipse:
			StrokeEllipse(box);
			break;
		case kToolLine:
		case kToolArrow:
			StrokeLine(a, b);
			break;
		case kToolInk:
			for (size_t i = 1; i < mToolPoints.size(); i++)
				StrokeLine(mToolPoints[i - 1] + offset, mToolPoints[i] + offset);
			break;
		default:
			break;
	}
	SetPenSize(1);
}


// The annotation that is selected, if the page has it (while the page is rendered anew it has not).
const DocAnnotation*
PDFView::SelectedAnnotation() const
{
	if (mAnnotationIndex < 0 || mDoc == NULL || !mDoc->CanEditAnnotations())
		return NULL;
	if (mActive == NULL || mActive->number != mInteractionPage)
		return NULL;	// it is on another page
	const DocAnnotation* annotation = mPage->AnnotationAt(mAnnotationIndex);
	if (annotation != NULL && (annotation->kind == kAnnotMarkup || annotation->kind == kAnnotOther))
		return NULL;
	return annotation;
}


// where the annotation is in the bitmap, in pixels with fractions
BRect
PDFView::AnnotationDeviceRect(const DocAnnotation* annotation) const
{
	BPoint a = mPage->PageToDev(fz_make_point(annotation->rect.x0, annotation->rect.y0));
	BPoint b = mPage->PageToDev(fz_make_point(annotation->rect.x1, annotation->rect.y1));
	return BRect(min_c(a.x, b.x), min_c(a.y, b.y), max_c(a.x, b.x), max_c(a.y, b.y));
}


static BPoint
HandlePosition(const BRect& r, int handle)
{
	float centerX = (r.left + r.right) / 2, centerY = (r.top + r.bottom) / 2;
	switch (handle) {
		case 1: return BPoint(r.left, r.top);          // north west
		case 2: return BPoint(centerX, r.top);         // north
		case 3: return BPoint(r.right, r.top);         // north east
		case 4: return BPoint(r.right, centerY);       // east
		case 5: return BPoint(r.right, r.bottom);      // south east
		case 6: return BPoint(centerX, r.bottom);      // south
		case 7: return BPoint(r.left, r.bottom);       // south west
		default: return BPoint(r.left, centerY);       // west
	}
}


// the handle at the point (in the bitmap), the inside for moving, none outside; a note has no handles to
// resize it, and an annotation that is flat has only those along it
int
PDFView::HandleAt(const DocAnnotation* annotation, BPoint point) const
{
	BRect r = AnnotationDeviceRect(annotation);
	if (annotation->kind != kAnnotNote) {
		bool flatHorizontally = r.Width() < 6, flatVertically = r.Height() < 6;
		for (int handle = kHandleNorthWest; handle < kHandleCount; handle++) {
			bool vertical = handle == kHandleNorth || handle == kHandleSouth;
			bool horizontal = handle == kHandleEast || handle == kHandleWest;
			if ((flatVertically && !horizontal) || (flatHorizontally && !vertical))
				continue;
			BPoint at = HandlePosition(r, handle);
			if (fabsf(point.x - at.x) <= 5 && fabsf(point.y - at.y) <= 5)
				return handle;
		}
	}
	// on the thing itself, which may be a thin line
	if (r.InsetByCopy(-4, -4).Contains(point))
		return kHandleMove;
	return kHandleNone;
}


void
PDFView::SelectAnnotation(int index)
{
	if (index == mAnnotationIndex)
		return;

	if (const DocAnnotation* old = SelectedAnnotation())
		Invalidate(AnnotationDeviceRect(old).InsetByCopy(-10, -10).OffsetByCopy(mLeft, mTop));
	mAnnotationIndex = index;
	if (index >= 0) {
		SelectNone();	// text and an annotation are not selected at the same time
		if (const DocAnnotation* annotation = SelectedAnnotation())
			Invalidate(AnnotationDeviceRect(annotation).InsetByCopy(-10, -10).OffsetByCopy(mLeft, mTop));
	}
	SelectionChanged();
}


// The mouse went down on an annotation that can be moved or on one of the handles of the selected one: starts
// to drag. Elsewhere the selection ends and the click is for something else.
bool
PDFView::BeginAnnotationDrag(BPoint point)
{
	if (mRendering || mDoc == NULL || !mDoc->CanEditAnnotations())
		return false;

	BPoint p = CorrectMousePos(point);
	const DocAnnotation* annotation = SelectedAnnotation();
	int handle = annotation != NULL ? HandleAt(annotation, p) : kHandleNone;
	if (handle == kHandleNone) {
		float tolerance = 5.0f / mPage->Scale();
		annotation = mPage->FindAnnotation(mPage->DevToPage(p), tolerance, true);
		if (annotation == NULL) {
			if (mAnnotationIndex >= 0)
				SelectAnnotation(-1);
			return false;
		}
		SelectAnnotation(annotation->index);
		handle = kHandleMove;
	}

	mAnnotationHandle = handle;
	mAnnotationDragStart = p;
	mAnnotationOriginal = mAnnotationPreview = AnnotationDeviceRect(annotation);
	SetAction(ANNOT_ACTION);
	SetMouseEventMask(B_POINTER_EVENTS);
	return true;
}


// The drag ended: the annotation goes where its outline is, if it was moved at all.
void
PDFView::FinishAnnotationDrag()
{
	BRect preview = mAnnotationPreview;
	BRect original = mAnnotationOriginal;
	int handle = mAnnotationHandle;
	mAnnotationHandle = kHandleNone;
	Invalidate((preview | original).InsetByCopy(-10, -10).OffsetByCopy(mLeft, mTop));

	const DocAnnotation* annotation = SelectedAnnotation();
	if (annotation == NULL
		|| (fabsf(preview.left - original.left) < 1.5f && fabsf(preview.top - original.top) < 1.5f
			&& fabsf(preview.right - original.right) < 1.5f && fabsf(preview.bottom - original.bottom) < 1.5f))
		return;
	if (!ConfirmEditable())
		return;

	// the new bounds in page space; a move keeps the size exact
	fz_rect bounds;
	if (handle == kHandleMove) {
		fz_point from = mPage->DevToPage(original.LeftTop()), to = mPage->DevToPage(preview.LeftTop());
		bounds = annotation->rect;
		float dx = to.x - from.x, dy = to.y - from.y;
		bounds = fz_make_rect(bounds.x0 + dx, bounds.y0 + dy, bounds.x1 + dx, bounds.y1 + dy);
	} else {
		fz_point a = mPage->DevToPage(preview.LeftTop()), b = mPage->DevToPage(preview.RightBottom());
		bounds = fz_make_rect(fminf(a.x, b.x), fminf(a.y, b.y), fmaxf(a.x, b.x), fmaxf(a.y, b.y));
	}

	int index = annotation->index;
	WaitForPage(true);
	if (mDoc->SetAnnotationBounds(ActivePage(), index, bounds, handle != kHandleMove))
		AnnotationsChanged(ActivePage());	// the selection stays
}


void
PDFView::ShowAnnotation(int page, int index)
{
	if (mDoc == NULL || page < 1 || page > mDoc->PageCount())
		return;

	if (!PageShown(page))
		SetPage(page);
	WaitForPage();
	if (PageSlot* slot = SlotForPage(page))
		ActivateSlot(slot);

	const DocAnnotation* annotation = mPage->AnnotationAt(index);
	if (annotation == NULL)
		return;

	if (mAnnotationIndex >= 0)
		SelectAnnotation(-1);

	if (annotation->kind == kAnnotMarkup && !annotation->quads.empty()) {
		// the text it marks is selected, as a found text is
		const fz_quad& first = annotation->quads.front();
		const fz_quad& last = annotation->quads.back();
		SelectFound(fz_make_point(first.ul.x + 0.5f, (first.ul.y + first.ll.y) / 2),
			fz_make_point(last.ur.x - 0.5f, (last.ur.y + last.lr.y) / 2));
		return;
	}

	SelectAnnotation(index);
	// and into view
	BRect r = AnnotationDeviceRect(annotation);
	BRect shown = r.OffsetByCopy(mLeft, mTop), bounds(Bounds());
	if (!bounds.Contains(shown))
		ScrollTo(r.left - 40, r.top - 60);
}


bool
PDFView::ShowTarget(const BMessage& target, bool mark)
{
	if (mDoc == NULL)
		return false;

	DocTarget where;
	if (!mDoc->ResolveTarget(target, &where))
		return false;

	// the words are looked for from the page that was named (also if they are not on it)
	if (!where.quote.IsEmpty() && ShowQuote(where.quote.String(), where.page, mark))
		return true;
	if (where.page < 1)
		return false;

	MoveToPage(where.page);
	if (where.hasRegion) {
		WaitForPage();
		// the place is marked for a moment
		ClearTargetRegion();
		mTargetPage = where.page;
		mTargetRegion = where.region;
		FlashTarget();
		BPoint corner = mPage->PageToDev(fz_make_point(where.region.x0, where.region.y0));
		BRect shown(corner.x + mLeft, corner.y + mTop, corner.x + mLeft + 10, corner.y + mTop + 10), bounds(Bounds());
		if (!bounds.Contains(shown))
			ScrollTo(corner.x - 40, corner.y - 60);
	}
	return true;
}


void
PDFView::CopyWebAnnotation(int page, int index)
{
	BMessage annotation;
	if (mDoc == NULL || !mDoc->WebAnnotationOf(page, index, &annotation)) {
		beep();
		return;
	}
	BString json = WebAnnotation::ToJson(annotation);
	CopyText(&json);
}


void
PDFView::DeleteSelectedAnnotation()
{
	const DocAnnotation* annotation = SelectedAnnotation();
	if (annotation == NULL || !ConfirmEditable())
		return;
	int index = annotation->index;
	WaitForPage(true);
	mAnnotationIndex = -1;
	if (mDoc->DeleteAnnotation(ActivePage(), index))
		AnnotationsChanged(ActivePage());
	else
		Redraw();
}


void
PDFView::DrawAnnotationSelection()
{
	rgb_color highlight = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
	BPoint offset(mLeft, mTop);

	if (mMouseAction == ANNOT_ACTION) {
		// the outline of where it goes
		SetHighColor(highlight);
		StrokeRect(mAnnotationPreview.InsetByCopy(-2, -2).OffsetByCopy(offset));
		return;
	}

	const DocAnnotation* annotation = SelectedAnnotation();
	if (annotation == NULL)
		return;

	BRect r = AnnotationDeviceRect(annotation);
	SetHighColor(highlight);
	StrokeRect(r.InsetByCopy(-2, -2).OffsetByCopy(offset));
	if (annotation->kind == kAnnotNote)
		return;

	bool flatHorizontally = r.Width() < 6, flatVertically = r.Height() < 6;
	for (int handle = kHandleNorthWest; handle < kHandleCount; handle++) {
		bool vertical = handle == kHandleNorth || handle == kHandleSouth;
		bool horizontal = handle == kHandleEast || handle == kHandleWest;
		if ((flatVertically && !horizontal) || (flatHorizontally && !vertical))
			continue;
		BPoint at = HandlePosition(r, handle) + offset;
		BRect box(at.x - 3, at.y - 3, at.x + 3, at.y + 3);
		SetHighColor(255, 255, 255);
		FillRect(box);
		SetHighColor(highlight);
		StrokeRect(box);
	}
}


void
PDFView::Redo()
{
	WaitForPage(true);
	mAnnotationIndex = -1;
	int page = mDoc->Redo();
	if (page > 0)
		AnnotationsChanged(page);
}


void PDFView::SelectionChanged() {
	PDFWindow* w = GetPDFWindow();
	if (w) {
		w->UpdateInputEnabler();
	}
}

///////////////////////////////////////////////////////////
void PDFView::SendDragMessage(uint32 protocol) {
	BRect selection = SelectionBounds();
	if (!selection.IsValid())
		return;

	mDragStarted = true;
	SetMouseEventMask(B_POINTER_EVENTS);
	if (protocol == B_SIMPLE_DATA) {
		BMessage drag(B_SIMPLE_DATA);
		drag.AddString("be:types", "text/plain");
		drag.AddString("be:types", B_FILE_MIME_TYPE);

		BTranslatorRoster *roster = BTranslatorRoster::Default();
		BBitmapStream stream(mBitmap);

		translator_info *outInfo;
		int32 outNumInfo;

		drag.AddString("be:filetypes", "text/plain");
		drag.AddString("be:type_descriptions", "Text");

		if ((B_OK == roster->GetTranslators(&stream, NULL, &outInfo, &outNumInfo)) &&
			(outNumInfo >= 1)) {
			for (int32 i = 0; i < outNumInfo; i++) {
				const translation_format *fmts;
				int32 num_fmts;
				roster->GetOutputFormats(outInfo[i].translator, &fmts, &num_fmts);
				for (int32 j = 0; j < num_fmts; j++) {
					if (strcmp(fmts[j].MIME, "image/x-be-bitmap") != 0) {
						drag.AddString("be:filetypes", fmts[j].MIME);
						drag.AddString("be:type_descriptions", fmts[j].name);
					}
				}
			}
			drag.AddInt32("be:actions", B_COPY_TARGET);
			drag.AddString("be:clip_name", "Untitled clipping");
			DragMessage(&drag, selection.OffsetByCopy(mLeft, mTop));
		}
		BBitmap *bm;
		stream.DetachBitmap(&bm);
		if (bm != mBitmap) delete bm;
	} else if (protocol == B_MIME_DATA) {
		BMessage drag(B_MIME_DATA);
		BString *str = GetSelectedText();
		if (str) {
			drag.AddInt32("be:actions", B_TRASH_TARGET);
			drag.AddData("text/plain", B_MIME_DATA, str->String(), str->Length());
			delete str;
			DragMessage(&drag, selection.OffsetByCopy(mLeft, mTop));
		}
	}
}

void PDFView::SendDataMessage(BMessage *reply) {
	BMessage data(B_MIME_DATA);

	entry_ref dir;
	BString name, filetype;
	if (B_OK != reply->FindString("be:filetypes", &filetype)) {
		return;
	}
	bool saveToFile = (B_OK == reply->FindRef("directory", &dir)) &&
					  (B_OK == reply->FindString("name", &name));

	if (filetype == "text/plain") {
		BString *str = GetSelectedText();
		if (str) {
			if (saveToFile) {
				// write text to file
				BDirectory d(&dir);
				BNode node(&d, name.String());
				// set mime type
				BNodeInfo info(&node);
				if (info.InitCheck() == B_OK) {
					info.SetType("text/plain");
				}
				// write data
				BFile file(&d, name.String(), B_WRITE_ONLY);
				file.Write(str->String(), str->Length());
			} else {
				// send text in message to target application
				data.AddString("text/plain", str->String());
				reply->SendReply(&data);
			}
			delete str;
		}
		return;
	}

	//~ sending image in message to target application not implemented
	if (!saveToFile) return;

	// copy selection to bitmap
	BBitmap *bitmap = CopyBitmapArea(mBitmap, SelectionBounds(), mWidth, mHeight);
	if (bitmap == NULL)
		return;

	BBitmapStream stream(bitmap); // destructor frees bitmap

	// identify type
	BTranslatorRoster *roster = BTranslatorRoster::Default();
	translator_info *outInfo;
	int32 outNumInfo;
	if ((B_OK == roster->GetTranslators(&stream, NULL, &outInfo, &outNumInfo)) &&
		(outNumInfo >= 1)) {

		for (int32 i = 0; i < outNumInfo; i++) {
			const translation_format *fmts;
			int32 num_fmts;
			roster->GetOutputFormats(outInfo[i].translator, &fmts, &num_fmts);
			for (int32 j = 0; j < num_fmts; j++) {
				if (strcmp(fmts[j].MIME, filetype.String()) == 0) {
					// save bitmap to file
					BDirectory d(&dir);
					BNode node(&d, name.String());
					// set mime type
					BNodeInfo info(&node);
					if (info.InitCheck() == B_OK) {
						info.SetType(fmts[j].MIME);
					}
					// write data
					BFile file(&d, name.String(), B_WRITE_ONLY);
					roster->Translate(&stream, NULL, NULL, &file, fmts[j].type);
					return;
				}
			}
		}
	}
}

///////////////////////////////////////////////////////////
void
PDFView::SetColorSpace(color_space colorSpace) {
	// the page is always rendered as B_RGB32, how it is shown is up to the app_server
	mColorSpace = colorSpace;
}

///////////////////////////////////////////////////////////
void
PDFView::UpdateSettings(GlobalSettings* settings) {
	mInvertVerticalScrolling = settings->GetInvertVerticalScrolling();
}


#ifdef TOJI_TESTING
// Test hook: "hey Toji 'TSTX' to Window 0 with cmd=select with x1=.. " drives the view like the user does and
// writes what happened to /tmp/ts_test.out.
static void
TestLog(const char* format, ...)
{
	FILE* out = fopen("/tmp/ts_test.out", "a");
	if (out == NULL)
		return;
	va_list args;
	va_start(args, format);
	vfprintf(out, format, args);
	va_end(args);
	fputc('\n', out);
	fclose(out);
}

bool DrawPageInSlices(Document* document, int page, double dpi, int rotation, BView* view,
	PrintingProgressWindow* progress);

// invokes the default button of every window that has one, except the main window
static int32
TestPressDialogs(void*)
{
	for (int i = 0; i < 120; i++) {
		snooze(500000);
		for (int32 w = 0; w < be_app->CountWindows(); w++) {
			BWindow* window = be_app->WindowAt(w);
			// the main window is locked by the thread that waits for the dialog
			if (window == NULL || window->LockWithTimeout(100000) != B_OK)
				continue;
			BButton* button = window->DefaultButton();
			if (button != NULL && button->IsEnabled() && !window->IsHidden()) {
				TestLog("pressing %s in [%s]", button->Label(), window->Title());
				button->Invoke();
			}
			window->Unlock();
		}
	}
	return 0;
}

// hey passes numbers as it likes
static float
TestNumber(BMessage* message, const char* name)
{
	float f;
	if (message->FindFloat(name, &f) == B_OK)
		return f;
	int32 i;
	if (message->FindInt32(name, &i) == B_OK)
		return i;
	const char* string;
	if (message->FindString(name, &string) == B_OK)
		return atof(string);
	return 0;
}

void
PDFView::TestCommand(BMessage* message)
{
	BString cmd;
	message->FindString("cmd", &cmd);
	float x1 = TestNumber(message, "x1"), y1 = TestNumber(message, "y1");
	float x2 = TestNumber(message, "x2"), y2 = TestNumber(message, "y2");
	int32 page = (int32)TestNumber(message, "page");

	// the page that a click at (x1, y1) would be on is the active one, as the mouse does it
	if (message->HasString("x1") || message->HasFloat("x1") || message->HasInt32("x1")) {
		if (PageSlot* slot = SlotAt(BPoint(x1, y1)))
			ActivateSlot(slot);
	}

	if (cmd == "select" || cmd == "area") {
		// as the secondary mouse button does it
		SetAction(SELECT_ACTION);
		mSelected = DO_SELECTION;
		mSelectionKind = cmd == "area" ? kSelectArea : kSelectText;
		BPoint start = LimitToPage(CorrectMousePos(BPoint(x1, y1)));
		mSelectionStart = start;
		mSelection.SetLeftTop(start);
		mSelection.SetRightBottom(start);
		if (mSelectionKind == kSelectText)
			StartTextSelection(start);
		ResizeSelection(BPoint(x2, y2));
		MouseUp(BPoint(x2, y2));
		BString* text = GetSelectedText();
		TestLog("%s (%g,%g)-(%g,%g): %d quads, text: [%s]", cmd.String(), x1, y1, x2, y2, (int)mQuads.size(),
			text != NULL ? text->String() : "(none)");
		delete text;
	} else if (cmd == "word" || cmd == "line") {
		bool ok = SelectTextAt(BPoint(x1, y1), cmd == "word" ? FZ_SELECT_WORDS : FZ_SELECT_LINES);
		BString* text = GetSelectedText();
		TestLog("%s at (%g,%g): %s, text: [%s]", cmd.String(), x1, y1, ok ? "ok" : "nothing", text != NULL ? text->String() : "(none)");
		delete text;
	} else if (cmd == "annotate") {
		// marks the selection: type= highlight, underline or strikeout (in "kind")
		BString kind;
		message->FindString("kind", &kind);
		MarkupType type = kind == "underline" ? kMarkupUnderline
			: kind == "strikeout" ? kMarkupStrikeOut : kMarkupHighlight;
		bool ok = AnnotateSelection(type, kind == "highlight" ? 0xffeb3b : 0xe53935);
		TestLog("annotate %s: %s, unsaved changes: %d", kind.String(), ok ? "ok" : "failed",
			(int)mDoc->HasUnsavedChanges());
	} else if (cmd == "armmarker") {
		// the marker of the toolbar with a color (kind: highlight, underline, strikeout)
		BString kind;
		message->FindString("kind", &kind);
		MarkupType type = kind == "underline" ? kMarkupUnderline : kind == "strikeout" ? kMarkupStrikeOut : kMarkupHighlight;
		ArmMarkup(type, type == kMarkupHighlight ? 0x78beff : 0xe53935);
		TestLog("armmarker %s: armed state %d", kind.String(), ArmedState());
	} else if (cmd == "armtool") {
		BString kind;
		message->FindString("kind", &kind);
		SetTool(kind == "note" ? kToolNote : kind == "text" ? kToolFreeText : kToolRectangle);
		TestLog("armtool %s: armed state %d", kind.String(), ArmedState());
	} else if (cmd == "marginnote") {
		MarginNoteOnSelection();
		TestLog("marginnote: unsaved changes %d", (int)mDoc->HasUnsavedChanges());
	} else if (cmd == "hovermargin") {
		// the pointer on the n-th note of the margin of the active page (-1: none)
		int32 n = (int32)TestNumber(message, "n");
		std::vector<MarginBox> boxes;
		MarginNoteBoxes(&boxes);
		mMarginHover = n >= 0 && n < (int32)boxes.size() ? boxes[n].annotation : NULL;
		Invalidate();
		TestLog("hovermargin: %d boxes", (int)boxes.size());
	} else if (cmd == "turn") {
		// a turn to the next page that stays at a progress (t=0..1); "turnend" lets go
		mTurnForced = true;
		mTurnFreeze = TestNumber(message, "t");
		if (message->HasString("back"))
			PreviousPage();
		else
			NextPage();
		TestLog("turn: state %d, page %d", mTurnState, mCurrentPage);
	} else if (cmd == "turnend") {
		mTurnFreeze = -1;
		mTurnForced = false;
		CancelTurn();
	} else if (cmd == "scrollto") {
		// the view to (x1, y1) of the canvas
		ScrollTo(x1, y1);
		TestLog("scrollto: top now %.0f", Bounds().top);
	} else if (cmd == "gotopos") {
		// as a link to a place on a page: gotopos with page, x and y of the page
		GotoPosition((int)TestNumber(message, "page"), TestNumber(message, "x"), TestNumber(message, "y"));
		TestLog("gotopos: top now %.0f, page %d", Bounds().top, mCurrentPage);
	} else if (cmd == "editnote") {
		// as a double click at (x1, y1) does
		TestLog("editnote: %d", (int)EditNoteAt(BPoint(x1, y1), true, false));
	} else if (cmd == "notebutton") {
		NoteButton(ConvertToScreen(BPoint(150, 60)));
		TestLog("notebutton: armed state %d", ArmedState());
	} else if (cmd == "markermenu") {
		ShowMarkerMenu(ConvertToScreen(BPoint(150, 60)));
	} else if (cmd == "shapesmenu") {
		ShowShapesMenu(ConvertToScreen(BPoint(150, 60)));
	} else if (cmd == "armstate") {
		TestLog("armed state %d", ArmedState());
	} else if (cmd == "showannot") {
		// what the list of annotations sends, or the id from outside (in "text")
		BString id;
		message->FindString("text", &id);
		BMessage show(PDFWindow::SHOW_ANNOTATION_CMD);
		if (id.Length() > 0)
			show.AddString("id", id);
		else {
			show.AddInt32("page", (int32)TestNumber(message, "page"));
			show.AddInt32("index", (int32)TestNumber(message, "which"));
		}
		GetPDFWindow()->MessageReceived(&show);
		TestLog("showannot: page %d, annotation selected %d, text selected %d", mCurrentPage, mAnnotationIndex,
			(int)HasTextSelection());
	} else if (cmd == "findannot") {
		BString id;
		message->FindString("text", &id);
		int foundPage = 0, foundIndex = -1;
		bool ok = mDoc->FindAnnotationById(id.String(), &foundPage, &foundIndex);
		TestLog("findannot [%s]: %s page %d index %d", id.String(), ok ? "found" : "not found", foundPage, foundIndex);
	} else if (cmd == "selectannot") {
		// as a click at (x1, y1) does it
		const DocAnnotation* hit = mPage->FindAnnotation(mPage->DevToPage(CorrectMousePos(BPoint(x1, y1))),
			5.0f / mPage->Scale(), true);
		SelectAnnotation(hit != NULL ? hit->index : -1);
		const DocAnnotation* selected = SelectedAnnotation();
		TestLog("selectannot at (%g,%g): %s, selected %d", x1, y1, hit != NULL ? "hit" : "nothing",
			selected != NULL ? selected->index : -1);
	} else if (cmd == "moveannot") {
		// a drag of the selected annotation: from (x1, y1) to (x2, y2), the start decides move or handle
		bool began = BeginAnnotationDrag(BPoint(x1, y1));
		if (began) {
			// the same arithmetic as MouseMoved does it, through its message
			BPoint p = CorrectMousePos(BPoint(x2, y2));
			BRect r = mAnnotationOriginal;
			float dx = p.x - mAnnotationDragStart.x, dy = p.y - mAnnotationDragStart.y;
			switch (mAnnotationHandle) {
				case kHandleMove: r.OffsetBy(dx, dy); break;
				case kHandleSouthEast: r.right += dx; r.bottom += dy; break;
				case kHandleNorthWest: r.left += dx; r.top += dy; break;
				case kHandleEast: r.right += dx; break;
				case kHandleSouth: r.bottom += dy; break;
				case kHandleWest: r.left += dx; break;
				case kHandleNorth: r.top += dy; break;
				default: break;
			}
			mAnnotationPreview = r;
			SetAction(NO_ACTION);
			TestLog("moveannot: handle %d, to %g,%g-%g,%g", mAnnotationHandle, r.left, r.top, r.right, r.bottom);
			FinishAnnotationDrag();
		} else
			TestLog("moveannot: nothing to drag at (%g,%g)", x1, y1);
	} else if (cmd == "deleteannot") {
		DeleteSelectedAnnotation();
		TestLog("deleteannot: selected now %d", mAnnotationIndex);
	} else if (cmd == "tool") {
		// kind: note, text, rectangle, ellipse, line, arrow, drawing
		BString kind;
		message->FindString("kind", &kind);
		PlacementTool tool = kind == "note" ? kToolNote : kind == "text" ? kToolFreeText
			: kind == "rectangle" ? kToolRectangle : kind == "ellipse" ? kToolEllipse
			: kind == "line" ? kToolLine : kind == "arrow" ? kToolArrow : kToolInk;
		SetTool(tool);
		TestLog("tool %s: active %d", kind.String(), (int)HasTool());
	} else if (cmd == "drag") {
		// a drag from (x1, y1) to (x2, y2) with the tool, as MouseDown, MouseMoved and MouseUp do it
		BeginTool(BPoint(x1, y1));
		if (mMouseAction == TOOL_ACTION) {
			for (int step = 1; step <= 10; step++) {
				BPoint p(x1 + (x2 - x1) * step / 10, y1 + (y2 - y1) * step / 10);
				// a drawing wiggles
				if (mTool == kToolInk)
					p.y += (step % 2) * 6;
				mToolEnd = LimitToPage(CorrectMousePos(p));
				if (mTool == kToolInk)
					mToolPoints.push_back(mToolEnd);
			}
			SetAction(NO_ACTION);
			// FinishTool needs the tool set, it is cleared by CancelTool there
			FinishTool(BPoint(x2, y2));
		}
		TestLog("drag: tool now %d", (int)mTool);
	} else if (cmd == "addtext") {
		// what the text window sends: kind note or text, at a place in the view
		BString kind, text;
		message->FindString("kind", &kind);
		message->FindString("text", &text);
		BPoint p = CorrectMousePos(BPoint(x1, y1));
		fz_point where = mPage->DevToPage(p);
		BMessage create(CREATE_TEXT_MSG);
		create.AddInt32("tool", kind == "text" ? kToolFreeText : kToolNote);
		create.AddInt32("page", mCurrentPage);
		create.AddFloat("x", where.x);
		create.AddFloat("y", where.y);
		create.AddString("text", text);
		MessageReceived(&create);
		TestLog("addtext %s at page %g,%g", kind.String(), where.x, where.y);
	} else if (cmd == "slots") {
		BString numbers;
		for (size_t i = 0; i < mSlots.size(); i++)
			numbers << mSlots[i]->number << (mSlots[i]->rendering ? "* " : " ");
		TestLog("slots %d: %s| current %d, active %d, interaction %d, scroll top %g", (int)mSlots.size(),
			numbers.String(), mCurrentPage, ActivePage(), mInteractionPage, Bounds().top);
		TestLog("  canvas %gx%g, flow %d, gap %s, zoom %d dpi", mCanvasWidth, mCanvasHeight, (int)mLayout.Flow(),
			mLayout.TopToBottom() ? "none" : "page gap", (int)GetZoomDPI());
		for (int page = 1; page <= GetNumPages() && page <= 3; page++) {
			BRect r = mLayout.PageRect(page);
			TestLog("  page %d: top %g bottom %g", page, r.top, r.bottom);
		}
	} else if (cmd == "titlealone") {
		SetTitlePageAlone(TestNumber(message, "which") != 0);
		TestLog("title page alone %d: page %d, canvas %gx%g", (int)TitlePageAlone(), mCurrentPage, mCanvasWidth,
			mCanvasHeight);
	} else if (cmd == "flow") {
		// a flow by its number, also those that have no button
		SetFlow((PageFlow)(int)TestNumber(message, "which"));
		TestLog("flow %d: page %d, canvas %gx%g, %d slots", (int)mLayout.Flow(), mCurrentPage, mCanvasWidth,
			mCanvasHeight, (int)mSlots.size());
	} else if (cmd == "history") {
		TestLog("history: undo %d [%s], redo %d [%s], unsaved %d, page %d", (int)mDoc->CanUndo(),
			mDoc->UndoLabel().String(), (int)mDoc->CanRedo(), mDoc->RedoLabel().String(),
			(int)mDoc->HasUnsavedChanges(), mCurrentPage);
	} else if (cmd == "popup") {
		// the context menu as a secondary click at (x1, y1) shows it (blocks until it is closed)
		BPoint where(x1, y1);
		ShowPopUpMenu(ConvertToScreen(where), OnLink(where), OnAnnotation(where));
	} else if (cmd == "colormenu") {
		// the items of the color submenu as a menu of their own, with "color" as the current one
		BMessage change(CHANGE_COLOR_MSG);
		BMenu* colors = BuildColorMenu("Color", change, this, true, (uint32)TestNumber(message, "color"));
		BPopUpMenu* popup = new BPopUpMenu("colors");
		popup->SetAsyncAutoDestruct(true);
		while (BMenuItem* item = colors->RemoveItem((int32)0))
			popup->AddItem(item);
		delete colors;
		popup->Go(ConvertToScreen(BPoint(x1, y1)), true, false, false);
	} else if (cmd == "setcolor") {
		bool ok = mDoc->SetAnnotationColor(mCurrentPage, (int)TestNumber(message, "which"),
			(uint32)TestNumber(message, "color"));
		if (ok)
			AnnotationsChanged();
		TestLog("setcolor: %s", ok ? "ok" : "failed");
	} else if (cmd == "hover") {
		// as if the mouse was at (x1, y1): cursor and tooltip
		DisplayLink(BPoint(x1, y1));
		ShowToolTip(ToolTip());	// a real mouse shows it when it rests
		TestLog("hover (%g,%g): note tip %d", x1, y1, mNoteTip);
	} else if (cmd == "allannots") {
		// all annotations of the document as the list gets them
		std::vector<DocAnnotationEntry> entries;
		bool ok = mDoc->ListAnnotations(entries, NULL);
		TestLog("allannots (%s) text size %g, %d pages: %d", ok ? "ok" : "failed", mDoc->TextSize(),
			mDoc->PageCount(), (int)entries.size());
		for (size_t i = 0; i < entries.size(); i++) {
			TestLog("  page %d #%d %s quads %d [%s]", entries[i].page, entries[i].annotation.index,
				entries[i].annotation.label.String(), (int)entries[i].annotation.quads.size(),
				entries[i].excerpt.String());
		}
	} else if (cmd == "cfiresolve") {
		// where a CFI leads (in "text")
		BString text;
		message->FindString("text", &text);
		int spine = -1;
		BString words;
		float fraction = 0;
		bool ok = mDoc->Epub() != NULL
			&& EpubCfi::Resolve(mDoc->ContentPath(), *mDoc->Epub(), text.String(), &spine, &words, &fraction);
		TestLog("cfiresolve [%s]: %s spine %d fraction %g words [%s]", text.String(), ok ? "ok" : "failed", spine,
			fraction, words.String());
	} else if (cmd == "anchor") {
		// the anchor of a page, and the page it leads to
		TextAnchor anchor;
		int pageNo = (int)TestNumber(message, "page");
		bool ok = mDoc->MakeAnchor(pageNo, &anchor);
		int back = ok ? mDoc->PageOfAnchor(anchor) : 0;
		TestLog("anchor of page %d: %s chapter %d fraction %g quote [%s] cfi [%s] -> page %d", pageNo,
			ok ? "ok" : "failed", (int)anchor.chapter, anchor.fraction, anchor.quote.String(), anchor.cfi.String(),
			back);
	} else if (cmd == "annots") {
		// what the page has, and what is under a point (x1, y1)
		WaitForPage();
		const std::vector<DocAnnotation>& list = mPage->mAnnotations;
		TestLog("annots on page %d: %d", mCurrentPage, (int)list.size());
		for (size_t i = 0; i < list.size(); i++) {
			TestLog("  #%d id [%s] cfi [%s] type %d markup %d quads %d color %s%06x rect %g,%g-%g,%g author [%s] note [%s]", list[i].index,
				list[i].id.String(), list[i].cfi.String(),
				list[i].type, (int)list[i].isMarkup, (int)list[i].quads.size(), list[i].hasColor ? "#" : "none ",
				(unsigned)list[i].color, list[i].rect.x0, list[i].rect.y0,
				list[i].rect.x1, list[i].rect.y1, list[i].author.String(), list[i].contents.String());
		}
		const DocAnnotation* under = OnAnnotation(BPoint(x1, y1));
		TestLog("  at (%g,%g): %s", x1, y1, under != NULL ? "annotation" : "nothing");
	} else if (cmd == "delannot") {
		bool ok = mDoc->DeleteAnnotation(mCurrentPage, (int)TestNumber(message, "which"));
		if (ok)
			AnnotationsChanged();
		TestLog("delannot: %s", ok ? "ok" : "failed");
	} else if (cmd == "setnote") {
		BString text;
		message->FindString("text", &text);
		bool ok = mDoc->SetAnnotationContents(mCurrentPage, (int)TestNumber(message, "which"), text.String());
		if (ok)
			AnnotationsChanged();
		TestLog("setnote: %s", ok ? "ok" : "failed");
	} else if (cmd == "selectall") {
		SelectAll();
		BString* text = GetSelectedText();
		TestLog("selectall: %d quads, %d chars", (int)mQuads.size(), text != NULL ? (int)text->Length() : 0);
		delete text;
	} else if (cmd == "selectnone") {
		SelectNone();
		TestLog("selectnone: %d quads", (int)mQuads.size());
	} else if (cmd == "link") {
		const DocLink* link = OnLink(BPoint(x1, y1));
		BString description;
		LinkToString(link, &description);
		bool handled = HandleLink(BPoint(x1, y1));
		TestLog("link at (%g,%g): %s -> %s, handled %d, page now %d", x1, y1, link != NULL ? link->uri.String() : "none",
			description.String(), handled, mCurrentPage);
	} else if (cmd == "links") {
		int n = 0;
		for (size_t i = 0; i < mPage->mLinks.size(); i++) {
			const DocLink& l = mPage->mLinks[i];
			BRect r = mPage->PageToDev(l.rect);
			int target; float x, y;
			bool internal = mDoc->ResolveLink(l.uri.String(), &target, &x, &y);
			if (n++ < 6)
				TestLog("link %d: [%s] at view %g,%g-%g,%g -> %s %d", (int)i, l.uri.String(), r.left, r.top, r.right,
					r.bottom, internal ? "page" : "external", internal ? target : 0);
		}
		TestLog("page %d has %d links", mCurrentPage, (int)mPage->mLinks.size());
	} else if (cmd == "printslices") {
		// the drawing of the printing, into a bitmap that is saved: x1 is the dpi, page the page
		bool drawn = false;
		fz_matrix matrix;
		int width = 0, height = 0;
		if (mDoc->PageMatrix(page, x1, (int)mRotation, &matrix, &width, &height)) {
			BBitmap bitmap(BRect(0, 0, width - 1, height - 1), B_RGB32, true);
			BView* view = new BView(bitmap.Bounds(), "print", B_FOLLOW_NONE, B_WILL_DRAW);
			if (bitmap.Lock()) {
				bitmap.AddChild(view);
				drawn = DrawPageInSlices(mDoc, page, x1, (int)mRotation, view, NULL);
				view->Sync();
				bitmap.RemoveChild(view);
				bitmap.Unlock();
			}
			delete view;
			BString path;
			path << "/tmp/printslices-" << (int)page << "-" << (int)x1 << ".png";
			BBitmapStream stream(&bitmap);
			BFile file(path.String(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
			status_t status = BTranslatorRoster::Default()->Translate(&stream, NULL, NULL, &file, B_PNG_FORMAT);
			BBitmap* detached;
			stream.DetachBitmap(&detached);
			TestLog("printslices page %d at %g dpi: %dx%d, drawn %d, saved %s", (int)page, x1, width, height, drawn,
				status == B_OK ? path.String() : "FAILED");
		}
	} else if (cmd == "print") {
		// presses the default button of the dialogs of the printing (page setup and job), then prints
		TestLog("print: starting");
		thread_id helper = spawn_thread(TestPressDialogs, "press dialogs", B_NORMAL_PRIORITY, NULL);
		resume_thread(helper);
		Print();
	} else if (cmd == "goto") {
		MoveToPage(page);
		TestLog("goto %d: page now %d", (int)page, mCurrentPage);
	} else if (cmd == "scrollto") {
		ScrollTo(x1, y1);
		TestLog("scrollto (%g,%g)", x1, y1);
	} else if (cmd == "zoom") {
		SetZoom(-(int)x1);
		TestLog("zoom %g dpi", x1);
	} else if (cmd == "dump") {
		BRect bounds = Bounds();
		TestLog("page %d of %d, bitmap %gx%g, left/top %g/%g, view bounds %g,%g-%g,%g, selected %d, rendering %d",
			mCurrentPage, GetNumPages(), mWidth, mHeight, mLeft, mTop, bounds.left, bounds.top, bounds.right,
			bounds.bottom, (int)mSelected, (int)mRendering);
	}
	Invalidate();
}
#endif
