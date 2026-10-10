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

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <ctype.h>

#include <locale/Catalog.h>
#include <be/app/Application.h>
#include <be/storage/Entry.h>
#include <be/storage/FilePanel.h>
#include <be/app/Roster.h>
#include <be/interface/Screen.h>
#include <be/StorageKit.h>
#include <fs_attr.h>
#include <Deskbar.h>
#include <be/interface/Alert.h>
#include <Bitmap.h>
#include <ControlLook.h>
#include <IconUtils.h>
#include <Resources.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <TranslationUtils.h>
#include <fs_index.h>
#include <Mime.h>
#include <MimeType.h>
#include <TextView.h>
#include <View.h>
#include <Window.h>

#include "PDFWindow.h"
#include "Application.h"
#include "DeepLink.h"
#include "WebAnnotation.h"
#include "ResourceLoader.h"
#include "PasswordWindow.h"
#include "Globals.h"
#include "TraceWindow.h"
#include "Document.h"
#include "ComicInfo.h"
#include "EpubInfo.h"
#include "FileInfoWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BepdfApplication"

static const char * tojiCopyright =
	"© 2026 Gregor B. Rosenauer & Claude\n";

// history of BePDF, newest first
static const char * bePDFCopyright =
    "© 2013-2017 waddlesplash\n"
    "© 2000-2011 Michael Pfeiffer\n"
	"© 1998-2000 Hubert Figuiere\n"
	"© 1997 Benoit Triquet\n";

static const char * licenseCopyright =
    "\n\n"
    "This program is free software under the GNU AGPL v3, or any later version.\n";

// Deep links are described like W3C Web Annotations (WebAnnotation.h). A B_REFS_RECEIVED message names the file in refs
// and says where to go in it with
//   oa:hasTarget    a message with oa:hasSelector entries: an oa:FragmentSelector (page=5, an EPUB CFI), an
//                   oa:TextQuoteSelector (the words), which may be refined by another selector (oa:refinedBy);
//   oa:motivatedBy  (a string, with oa:hasTarget) the passage that the words name is marked for a moment (a highlight
//                   that is no annotation: the document stays as it is);
//   oa:Annotation   the identifier of an annotation (an IRI: urn:sen:<tsid>): the document goes to it and selects it.
static const char *TARGET_MSG_KEY = "oa:hasTarget";
static const char *MOTIVATION_MSG_KEY = "oa:motivatedBy";
static const char *ANNOTATION_MSG_KEY = "oa:Annotation";

static const char *settingsFilename = "Toji";

// Implementation of PDFFilter
class PDFFilter : public BRefFilter {
	static const char *valid_filetypes[];

public:
	bool Filter(const entry_ref *ref, BNode *node, struct stat_beos *st, const char *filetype);
};

static PDFFilter pdfFilter;

BRefFilter* GetPdfFilter() {
	return &pdfFilter;
}

const char * PDFFilter::valid_filetypes[] = {
	"application/x-vnd.Be-directory",
	"application/x-vnd.Be-symlink",
	"application/x-vnd.Be-volume",
	"application/pdf",
	"application/x-pdf",
	NULL
};

bool PDFFilter::Filter(const entry_ref *ref, BNode *node, struct stat_beos *st, const char *filetype) {
	for (int i = 0; valid_filetypes[i]; i++) {
		if (strcmp(filetype, valid_filetypes[i]) == 0) return true;
		// check file extension if filetype has not been set to application/pdf
		BString name(ref->name);
		name.ToUpper();
		int32 l = name.FindLast('.');
		if (l != B_ERROR) {
			if (name.FindFirst(".PDF", l) != B_ERROR) return true;
		}
	}
	return false;
}
///////////////////////////////////////////////////////////
int main()
{
	new BepdfApplication ( );

	be_app->Run();

	delete be_app;
	return 0;
}



// What a document says about itself, as BFS attributes. Nothing is invented: the names are the properties of the
// ontologies that are in use everywhere, with the prefix that is commonly used for each of them: dc: (Dublin Core
// elements), dcterms: (Dublin Core terms) and schema: (schema.org); foaf: and others come the same way when they are
// needed. Where there is no such property the prefix is SEN: (or PDF: for what only a PDF file has).
struct BookAttribute {
	const char* name;
	const char* label;
	int32       type;
	int32       width;
};

static const BookAttribute kBookAttributes[] = {
	{ "dc:title", B_TRANSLATE_MARK("Title"), B_STRING_TYPE, 150 },
	{ "dc:creator", B_TRANSLATE_MARK("Author"), B_STRING_TYPE, 150 },
	{ "dc:subject", B_TRANSLATE_MARK("Keywords"), B_STRING_TYPE, 150 },
	{ "schema:numberOfPages", B_TRANSLATE_MARK("Pages"), B_INT32_TYPE, 60 },
	{ "dc:description", B_TRANSLATE_MARK("Description"), B_STRING_TYPE, 200 },
	{ "dc:publisher", B_TRANSLATE_MARK("Publisher"), B_STRING_TYPE, 150 },
	{ "dc:language", B_TRANSLATE_MARK("Language"), B_STRING_TYPE, 60 },
	{ "dc:date", B_TRANSLATE_MARK("Published"), B_TIME_TYPE, 100 },
	{ "dc:identifier", B_TRANSLATE_MARK("Identifier"), B_STRING_TYPE, 200 },
	{ "schema:isbn", B_TRANSLATE_MARK("ISBN"), B_STRING_TYPE, 110 },
	{ "dcterms:isPartOf", B_TRANSLATE_MARK("Series"), B_STRING_TYPE, 150 },	// the series the book belongs to
	{ "schema:position", B_TRANSLATE_MARK("Series number"), B_DOUBLE_TYPE, 60 },
	// rtl, ltr or default, the page progression of the EPUB standard (a manga is read from the right to the left)
	{ "SEN:readingProgression", B_TRANSLATE_MARK("Reading direction"), B_STRING_TYPE, 90 }
};
static const size_t kBookAttributeCount = sizeof(kBookAttributes) / sizeof(kBookAttributes[0]);

// how many annotations the document has (any kind of document), for queries; the annotations of a book are in
// SEN:annotations. Nothing in the ontologies is made for either, so they have the prefix of SEN.
static const char* const kAnnotationCountAttribute = "SEN:annotationCount";
// ... and how many bookmarks (not indexed: it is for showing)
static const char* const kBookmarkCountAttribute = "SEN:bookmarkCount";


// CBZ, CBR and CBT are ZIP, RAR and TAR files without a mark of their own. What they have in common is that the first
// file in the archive is a page (or ComicInfo.xml): its name, with the extension of an image, is near the start of
// the file, where the header of the first entry is. The sniffer rule says that, in a priority above the one of the
// archive types (ZIP 0.4, RAR 0.5). 7z has its headers at the end and no mark, so CB7 is known by its extension.
static const struct {
	const char* type;
	const char* extension;
	const char* description;
	const char* signature;		// of the archive, in the sniffer rule syntax; NULL if the type has no rule
	const char* range;			// where the name of the first file is
} kComicTypes[] = {
	{ "application/vnd.comicbook+zip", "cbz", B_TRANSLATE_MARK("Comic book (ZIP)"), "\"PK\\003\\004\"", "[30:300]" },
	{ "application/vnd.comicbook-rar", "cbr", B_TRANSLATE_MARK("Comic book (RAR)"), "\"Rar!\"", "[7:400]" },
	{ "application/x-cb7", "cb7", B_TRANSLATE_MARK("Comic book (7z)"), NULL, NULL },
	{ "application/x-cbt", "cbt", B_TRANSLATE_MARK("Comic book (TAR)"), "[257] \"ustar\"", "[0:100]" },
	// the Bound Book Format has a mark of its own, at the start of the file
	{ "application/x-bbf", "bbf", B_TRANSLATE_MARK("Comic book (BBF)"), "\"BBF3\"", NULL },
	// DjVu is known to Haiku (type, extension, mark); it is here so that the attributes are defined and Toji is offered
	{ "image/vnd.djvu", "djvu", B_TRANSLATE_MARK("DjVu document"), NULL, NULL }
};
static const size_t kComicTypeCount = sizeof(kComicTypes) / sizeof(kComicTypes[0]);


static BString
ComicSnifferRule(const char* signature, const char* range)
{
	if (range == NULL)
		return BString("1.0 (") << signature << ")";	// a mark of its own
	static const char* const kNames[] = { ".jpg", ".jpeg", ".png", ".gif", ".webp", ".avif", ".bmp", ".tif", ".tiff",
		"comicinfo.xml", NULL };
	BString rule("0.60 (");
	rule << signature << ") (-i ";
	for (int i = 0; kNames[i] != NULL; i++) {
		if (i > 0)
			rule << " | ";
		rule << range << " \"" << kNames[i] << "\"";
	}
	rule << ")";
	return rule;
}


static void
AddAttrInfo(BMessage* info, const char* name, const char* label, int32 type, int32 width)
{
	info->AddString("attr:name", name);
	info->AddString("attr:public_name", B_TRANSLATE_NOCOLLECT(label));
	info->AddInt32("attr:type", type);
	info->AddInt32("attr:width", width);
	info->AddInt32("attr:alignment", B_ALIGN_LEFT);
	info->AddBool("attr:viewable", true);
	info->AddBool("attr:editable", false);
	info->AddBool("attr:extra", false);
}


// Haiku does not know EPUB files (one that is not compressed would be taken for a web page), so the type is made
// known, by its extension and by the name of the first file of the container. Which application opens them is
// left to the user.
static void
InstallMimeTypes(const entry_ref* application, bool keepLegacyColumns)
{
	BMimeType epub("application/epub+zip");
	if (epub.InitCheck() != B_OK)
		return;
	if (!epub.IsInstalled()) {
		if (epub.Install() != B_OK)
			return;
		epub.SetShortDescription(B_TRANSLATE("EPUB e-book"));
		epub.SetLongDescription(B_TRANSLATE("Electronic publication (EPUB)"));
		BMessage extensions;
		extensions.AddString("extensions", "epub");
		epub.SetFileExtensions(&extensions);
	}
	BMessage pdfInfo, epubInfo;
	BMimeType pdf("application/pdf");
	pdf.GetAttrInfo(&pdfInfo);
	for (size_t i = 0; i < kBookAttributeCount; i++)
		AddAttrInfo(&epubInfo, kBookAttributes[i].name, kBookAttributes[i].label, kBookAttributes[i].type,
			kBookAttributes[i].width);
	AddAttrInfo(&epubInfo, kAnnotationCountAttribute, B_TRANSLATE_MARK("Annotations"), B_INT32_TYPE, 70);
	AddAttrInfo(&epubInfo, kBookmarkCountAttribute, B_TRANSLATE_MARK("Bookmarks"), B_INT32_TYPE, 70);
	epub.SetAttrInfo(&epubInfo);

	// The same attributes for PDF files. What the type had (the PDF: attributes of the producer and the dates) stays; the
	// META: attributes of PDF files, which BePDF made up, are the properties of the ontologies now (dc:title, dc:creator,
	// dc:subject, dc:description, schema:numberOfPages, and PDF:creator for the program that made the document). The files
	// are not changed unless the user wants that (the setting), so their columns stay as long as they are kept.
	{
		BMessage pdfNew;
		const char* name;
		for (int32 i = 0; pdfInfo.FindString("attr:name", i, &name) == B_OK; i++) {
			if ((!keepLegacyColumns && strncmp(name, "META:", 5) == 0) || strcmp(name, kAnnotationCountAttribute) == 0
				|| strcmp(name, kBookmarkCountAttribute) == 0 || strcmp(name, "PDF:creator") == 0)
				continue;
			bool ours = false;
			for (size_t k = 0; k < kBookAttributeCount; k++) {
				if (strcmp(name, kBookAttributes[k].name) == 0)
					ours = true;
			}
			if (ours)
				continue;
			const char* publicName = name;
			int32 type = B_STRING_TYPE, width = 150, alignment = B_ALIGN_LEFT;
			bool viewable = true, editable = false, extra = false;
			pdfInfo.FindString("attr:public_name", i, &publicName);
			pdfInfo.FindInt32("attr:type", i, &type);
			pdfInfo.FindInt32("attr:width", i, &width);
			pdfInfo.FindInt32("attr:alignment", i, &alignment);
			pdfInfo.FindBool("attr:viewable", i, &viewable);
			pdfInfo.FindBool("attr:editable", i, &editable);
			pdfInfo.FindBool("attr:extra", i, &extra);
			pdfNew.AddString("attr:name", name);
			pdfNew.AddString("attr:public_name", publicName);
			pdfNew.AddInt32("attr:type", type);
			pdfNew.AddInt32("attr:width", width);
			pdfNew.AddInt32("attr:alignment", alignment);
			pdfNew.AddBool("attr:viewable", viewable);
			pdfNew.AddBool("attr:editable", editable);
			pdfNew.AddBool("attr:extra", extra);
		}
		for (size_t i = 0; i < kBookAttributeCount; i++)
			AddAttrInfo(&pdfNew, kBookAttributes[i].name, kBookAttributes[i].label, kBookAttributes[i].type,
				kBookAttributes[i].width);
		AddAttrInfo(&pdfNew, "PDF:creator", B_TRANSLATE_MARK("Creator"), B_STRING_TYPE, 120);
		AddAttrInfo(&pdfNew, kAnnotationCountAttribute, B_TRANSLATE_MARK("Annotations"), B_INT32_TYPE, 70);
		AddAttrInfo(&pdfNew, kBookmarkCountAttribute, B_TRANSLATE_MARK("Bookmarks"), B_INT32_TYPE, 70);
		pdf.SetAttrInfo(&pdfNew);
	}

	BString rule;
	if (epub.GetSnifferRule(&rule) != B_OK || rule.Length() == 0)
		epub.SetSnifferRule("1.0 [30] ('mimetypeapplication/epub+zip')");

	// Comic books are archives of images with the same attributes as books. Their names, as the freedesktop.org
	// database has them (Haiku knows nothing of them, they would be ZIP, RAR or TAR files).
	for (size_t i = 0; i < kComicTypeCount; i++) {
		BMimeType comic(kComicTypes[i].type);
		if (comic.InitCheck() != B_OK)
			continue;
		if (!comic.IsInstalled()) {
			if (comic.Install() != B_OK)
				continue;
			comic.SetShortDescription(B_TRANSLATE_NOCOLLECT(kComicTypes[i].description));
			comic.SetLongDescription(B_TRANSLATE_NOCOLLECT(kComicTypes[i].description));
			BMessage extensions;
			extensions.AddString("extensions", kComicTypes[i].extension);
			comic.SetFileExtensions(&extensions);
		}
		comic.SetAttrInfo(&epubInfo);

		if (kComicTypes[i].signature != NULL) {
			BString rule = ComicSnifferRule(kComicTypes[i].signature, kComicTypes[i].range);
			BString current;
			if (comic.GetSnifferRule(&current) != B_OK || current != rule)
				comic.SetSnifferRule(rule.String());
		}
	}

	// Mobipocket books: the same attributes as for EPUB (the database may know the type: then its names stay)
	BMimeType mobi("application/x-mobipocket-ebook");
	if (mobi.InitCheck() == B_OK) {
		if (!mobi.IsInstalled() && mobi.Install() == B_OK) {
			mobi.SetShortDescription(B_TRANSLATE("Mobipocket e-book"));
			mobi.SetLongDescription(B_TRANSLATE("Mobipocket e-book (MOBI)"));
			BMessage extensions;
			extensions.AddString("extensions", "mobi");
			extensions.AddString("extensions", "prc");
			extensions.AddString("extensions", "azw");
			extensions.AddString("extensions", "azw3");
			mobi.SetFileExtensions(&extensions);
		}
		mobi.SetAttrInfo(&epubInfo);
		BString mobiRule;
		if (mobi.GetSnifferRule(&mobiRule) != B_OK || mobiRule.Length() == 0)
			mobi.SetSnifferRule("1.0 [60] ('BOOKMOBI')");
	}

	// The database knows what an application supports from the entry of its signature, which is only made
	// when the application is entered (mimeset -a). Nobody does that for an application that comes in a package
	// or is built, so the entry is out of date after a new type has been added: Toji would not be offered for
	// EPUB files (Open with...). It is entered here, if it is not a supporting application of a type it names.
	// the links of Toji (toji:///path/doc.pdf#page=5, see lib/DeepLink.h): a program that opens such a link (a browser, a
	// mail program) starts what is named for the type of the scheme. It is a type of our own, so Toji is the one for it.
	BMimeType link("application/x-vnd.Be.URL.toji");
	if (link.InitCheck() == B_OK) {
		if (!link.IsInstalled()) {
			link.Install();
			link.SetShortDescription(B_TRANSLATE("Toji link"));
			link.SetLongDescription(B_TRANSLATE("Link to a place in a document (toji:)"));
		}
		char preferred[B_MIME_TYPE_LENGTH];
		if (link.GetPreferredApp(preferred) != B_OK)
			link.SetPreferredApp(BEPDF_APP_SIG);
	}

	bool listed = true;
	for (size_t i = 0; application != NULL && i <= kComicTypeCount + 1 && listed; i++) {
		BMimeType type(i == 0 ? "application/epub+zip" : i <= kComicTypeCount ? kComicTypes[i - 1].type
			: "application/x-vnd.Be.URL.toji");
		BMessage apps;
		listed = false;
		if (type.GetSupportingApps(&apps) == B_OK) {
			const char* signature;
			for (int32 k = 0; apps.FindString("applications", k, &signature) == B_OK; k++) {
				if (strcasecmp(signature, BEPDF_APP_SIG) == 0)
					listed = true;
			}
		}
	}
	if (application != NULL && !listed) {
		BPath path(application);
		if (path.InitCheck() == B_OK)
			create_app_meta_mime(path.Path(), false, true, true);
	}
}


///////////////////////////////////////////////////////////
BepdfApplication::BepdfApplication()
		: BApplication ( BEPDF_APP_SIG )
{
	mSettings = new GlobalSettings();
	mOpenFilePanel            = NULL;
	mSaveFilePanel            = NULL;
	mSaveToDirectoryFilePanel = NULL;
	mInitialized  = false;
	mGotSomething = false;
	mReadyToQuit  = false;
	mWindow = NULL;
	mAppRef = entry_ref();

	mStdoutTracer = NULL;
	mStderrTracer = NULL;
	pointerCursor = new BCursor(B_CURSOR_ID_SYSTEM_DEFAULT);
	linkCursor = new BCursor(B_CURSOR_ID_CREATE_LINK);
	handCursor = new BCursor(B_CURSOR_ID_GRAB);
	grabCursor = new BCursor(B_CURSOR_ID_GRABBING);
	textSelectionCursor = new BCursor(B_CURSOR_ID_I_BEAM);
	zoomCursor = new BCursor(B_CURSOR_ID_ZOOM_IN);
	splitVCursor = new BCursor(B_CURSOR_ID_RESIZE_NORTH_SOUTH);
	resizeCursor = new BCursor(B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST);

	BEntry entry; app_info info;
	if (B_OK == be_app->GetAppInfo(&info)) {
		mTeamID = info.team;
		mAppRef = info.ref;
		entry = BEntry(&info.ref);
		entry.GetPath(&mAppPath);
		mAppPath.GetParent(&mAppPath);
	} else {
		mAppPath.SetTo(".");
	}

	mDefaultPDF = mAppPath;
	mDefaultPDF.Append("docs/toji-start.pdf");		// the start page (the user guide is in the Help menu)

	BPath path(mAppPath);
	LoadSettings();
	InstallMimeTypes(mAppRef.device >= 0 ? &mAppRef : NULL, mSettings->GetLegacyAttributes() != 2);

	InitBePDF();
}

void
BepdfApplication::Initialize()
{
	mInitialized = true;
}

///////////////////////////////////////////////////////////
BepdfApplication::~BepdfApplication()
{
	SaveSettings();

	delete mSettings; mSettings = NULL;

	delete linkCursor;          linkCursor = NULL;
	delete handCursor;          handCursor = NULL;
	delete grabCursor;          grabCursor = NULL;
	delete textSelectionCursor; textSelectionCursor = NULL;
	delete zoomCursor;          zoomCursor = NULL;
	delete splitVCursor;        splitVCursor = NULL;
	delete resizeCursor;        resizeCursor = NULL;

	ExitBePDF();
}


///////////////////////////////////////////////////////////
void BepdfApplication::ReadyToRun()
{
#if 1
	mStdoutTracer = new OutputTracer(1, "stdout", GetSettings());
	mStderrTracer = new OutputTracer(2, "stderr", GetSettings());
#else
	mStdoutTracer = mStderrTracer = NULL;
#endif

	Initialize();
	if (! mGotSomething) {
		// open start document
		entry_ref defaultDocument;
		BMessage msg(B_REFS_RECEIVED);
		get_ref_for_path (mDefaultPDF.Path(), &defaultDocument);
		msg.AddRef ("refs", &defaultDocument);
		RefsReceived (&msg);

		if (!mGotSomething) {
			// on error open file open dialog
			OpenFilePanel();
		}
	}
}

///////////////////////////////////////////////////////////
// grey stripe with the app icon centered on its right border, with its center
// at a third of the height; the view reserves the room for the half of the icon sticking out
class AboutStripeView : public BView {
public:
	AboutStripeView(BBitmap *icon)
		: BView("stripe", B_WILL_DRAW), mIcon(icon)
	{
		float spacing = be_control_look->DefaultLabelSpacing();
		float half = (mIcon ? (mIcon->Bounds().Width() + 1) / 2 : 0);
		mStripeWidth = floorf(half + 2 * spacing);
		float width = mStripeWidth + half;
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetExplicitMinSize(BSize(width, B_SIZE_UNSET));
		SetExplicitMaxSize(BSize(width, B_SIZE_UNSET));
	}

	~AboutStripeView() { delete mIcon; }

	void Draw(BRect updateRect)
	{
		BRect stripe = Bounds();
		stripe.right = mStripeWidth - 1;
		SetHighColor(tint_color(ViewColor(), B_DARKEN_1_TINT));
		FillRect(stripe);
		if (mIcon == NULL)
			return;
		BRect b = Bounds(), i = mIcon->Bounds();
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		DrawBitmap(mIcon, BPoint(floorf(mStripeWidth - (i.Width() + 1) / 2),
			floorf(b.Height() / 3 - (i.Height() + 1) / 2)));
	}

private:
	BBitmap *mIcon;
	float mStripeWidth;
};

///////////////////////////////////////////////////////////
// the logo, scaled to the room it has (it is a picture with a white background, shown as a card)
class AboutLogoView : public BView {
public:
	AboutLogoView(BBitmap *logo, float height)
		: BView("logo", B_WILL_DRAW), mLogo(logo)
	{
		float width = height;
		if (mLogo != NULL && mLogo->Bounds().Height() > 0)
			width = floorf(height * (mLogo->Bounds().Width() + 1) / (mLogo->Bounds().Height() + 1));
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetExplicitMinSize(BSize(width, height));
		SetExplicitMaxSize(BSize(width, height));
		SetExplicitPreferredSize(BSize(width, height));
	}

	~AboutLogoView() { delete mLogo; }

	void Draw(BRect)
	{
		if (mLogo == NULL)
			return;
		BRect b = Bounds();
		SetDrawingMode(B_OP_COPY);
		DrawBitmap(mLogo, mLogo->Bounds(), b, B_FILTER_BITMAP_BILINEAR);
		SetHighColor(tint_color(ViewColor(), B_DARKEN_2_TINT));
		StrokeRect(b);
	}

private:
	BBitmap *mLogo;
};

static BMessenger sAboutWindow;
static const char *kTitleFamily = "Noto Emoji";
static const char *kTitleStyle = "Bold";

void BepdfApplication::AboutRequested()
{
	// only one About window at a time
	BLooper *looper = NULL;
	if (sAboutWindow.IsValid() && sAboutWindow.Target(&looper) != NULL && looper != NULL
		&& looper->Lock()) {
		BWindow *open = dynamic_cast<BWindow*>(looper);
		if (open != NULL)
			open->Activate();
		looper->Unlock();
		return;
	}

	BString version;
	BString str("Toji™\n\n");
	str += B_TRANSLATE("a universal document reader based on BePDF, extended for SEN");
	str += "\n";
	str += B_TRANSLATE("Version");
	str += " ";
	str += GetVersion(version);
	str += "\n";
	str += tojiCopyright;
	str += "\n";

	str += bePDFCopyright;
	str += "\n";

	str += BString().SetToFormat(B_TRANSLATE_COMMENT("Toji renders with MuPDF %s, %s.", "MuPDF version, copyright"),
		FZ_VERSION, "© Artifex Software, Inc.");

	str += "\n\n";
	str += B_TRANSLATE("Toji™, SEN™ and SEN Labs™ are names of SEN Labs e.U.");
	str += licenseCopyright;

	float spacing = be_control_look->DefaultLabelSpacing();
	float textWidth = be_plain_font->StringWidth("M") * 42;

	BTextView *v = new BTextView(BRect(0, 0, textWidth, 100), "text", BRect(0, 0, textWidth, 100),
		B_FOLLOW_NONE, B_WILL_DRAW);
	v->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	v->SetWordWrap(true);
	v->MakeEditable(false);
	v->MakeSelectable(false);
	v->SetStylable(true);
	v->SetText(str.String());

	rgb_color red = {255, 0, 51, 255};
	rgb_color blue = {0, 102, 255, 255};
	char *text = (char*)v->Text();
	char *s = text;
	// set all Be in BePDF in blue and red
	while ((s = strstr(s, "BePDF")) != NULL) {
		int32 i = s - text;
		v->SetFontAndColor(i, i+1, NULL, 0, &blue);
		v->SetFontAndColor(i+1, i+2, NULL, 0, &red);
		s += 2;
	}
	// first text line: the name, large and bold, set slightly apart from the rest
	s = strchr(text, '\n');
	int32 titleEnd = s - text + 1;
	BFont titleFont(be_plain_font);
	titleFont.SetFamilyAndStyle(kTitleFamily, kTitleStyle);
	titleFont.SetSize(be_plain_font->Size() * 2);
	v->SetFontAndColor(0, titleEnd, &titleFont, B_FONT_ALL);
	// the trademark sign is the Unicode character of the ordinary font (the emoji font has a picture of "TM" for it)
	BFont markFont(be_bold_font);
	markFont.SetSize(be_plain_font->Size() * 2);
	const char* mark = strstr(text, "\xE2\x84\xA2");
	if (mark != NULL && mark - text < titleEnd)
		v->SetFontAndColor(mark - text, mark - text + 3, &markFont, B_FONT_ALL);
	// the empty line after it is just a small gap
	BFont gapFont(be_plain_font);
	gapFont.SetSize(be_plain_font->Size() * 0.6);
	v->SetFontAndColor(titleEnd, titleEnd + 1, &gapFont, B_FONT_ALL);

	float textHeight = v->TextHeight(0, v->CountLines() - 1);
	v->SetExplicitMinSize(BSize(textWidth, textHeight));
	v->SetExplicitMaxSize(BSize(textWidth, textHeight));

	// the logo is a file next to the program (docs/toji-logo.png, in the package); without it the window has none
	BPath logoPath(mAppPath);
	logoPath.Append("docs/toji-logo.png");
	BBitmap *logo = BTranslationUtils::GetBitmap(logoPath.Path());

	BWindow *about = new BWindow(BRect(0, 0, 100, 100), B_TRANSLATE("About Toji"),
		B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE);
	BButton *ok = new BButton("ok", B_TRANSLATE("OK"), new BMessage(B_QUIT_REQUESTED));
	ok->MakeDefault(true);

	BLayoutBuilder::Group<>(about, B_HORIZONTAL, 0)
		.Add(new AboutStripeView(NULL))
		.AddGroup(B_VERTICAL, spacing)
			.SetInsets(spacing * 2)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(new AboutLogoView(logo, 200))
				.AddGlue()
			.End()
			.Add(v)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(ok)
			.End()
		.End();

	sAboutWindow = BMessenger(about);
	about->CenterOnScreen();
	about->Show();
}

/*
	open a file panel and ask for a PDF file
	the file panel will tell by itself if openning have been cancelled
	or not.
*/
void BepdfApplication::OpenFilePanel ()
{
	if (mOpenFilePanel == NULL) {
		mOpenFilePanel = new BFilePanel (B_OPEN_PANEL,
						NULL, NULL, B_FILE_NODE, true, NULL, NULL);
		mOpenFilePanel->SetRefFilter(&pdfFilter);
	}
	mOpenFilePanel->SetPanelDirectory(mSettings->GetPanelDirectory());
	mReadyToQuit = true;
	mOpenFilePanel->Show();
}

/*
	open a file panel and ask for a PDF file
	the file panel will tell by itself if openning have been cancelled
	or not.
*/
void BepdfApplication::OpenSaveFilePanel(BHandler* handler, bool fileMode, BRefFilter* filter, BMessage* msg, const char* name) {
	BFilePanel* panel = NULL;

	// lazy construct file panel
	if (fileMode) {
		// file panel for selection of file
		if (mSaveFilePanel == NULL) {
			mSaveFilePanel = new BFilePanel (B_SAVE_PANEL,
							NULL, NULL, B_FILE_NODE, false, NULL, NULL, true);
		}

		// hide other file panel
		if (mSaveToDirectoryFilePanel != NULL && mSaveToDirectoryFilePanel->IsShowing()) {
			mSaveToDirectoryFilePanel->Hide();
		}

		panel = mSaveFilePanel;
	} else {
		// file panel for selection of directory
		if (mSaveToDirectoryFilePanel == NULL) {
			mSaveToDirectoryFilePanel = new BFilePanel (B_OPEN_PANEL,
							NULL, NULL, B_DIRECTORY_NODE, false, NULL, NULL, true);
		}

		// hide other file panel
		if (mSaveFilePanel != NULL && mSaveFilePanel->IsShowing()) {
			mSaveFilePanel->Hide();
		}

		panel = mSaveToDirectoryFilePanel;
	}

	// (re)-set to directory of currently opened PDF file
	// TODO decide if directory should be independent from PDF file
	panel->SetPanelDirectory(mSettings->GetPanelDirectory());

	if (name != NULL) {
		panel->SetSaveText(name);
	}
	else if (fileMode) {
		panel->SetSaveText("");
	}

	// set/reset filter
	panel->SetRefFilter(filter);

	// add kind to message
	BMessage message(B_SAVE_REQUESTED);
	if (msg == NULL) {
		msg = &message;
	}
	panel->SetMessage(msg);

	// set target
	BMessenger msgr(handler);
	panel->SetTarget(msgr);

	panel->Refresh();

	panel->Show();
}

void BepdfApplication::OpenSaveFilePanel(BHandler* handler, BRefFilter* filter, BMessage* msg, const char* name) {
	OpenSaveFilePanel(handler, true, filter, msg, name);
}

void BepdfApplication::OpenSaveToDirectoryFilePanel(BHandler* handler, BRefFilter* filter, BMessage* msg, const char* name) {
	OpenSaveFilePanel(handler, false, filter, msg, name);
}


/*
  NOTIFY_QUIT_MSG:
  Or to quit all BePDF applications.
*/
void BepdfApplication::Notify(uint32 cmd) {
	BList list;
	be_roster->GetAppList(BEPDF_APP_SIG, &list);
	const int n = list.CountItems()-1;
	BMessage msg(cmd);
	// notify all but this team
	for (int i = n; i >= 0; i --) {
		team_id who = (team_id)(addr_t)list.ItemAt(i);
		if (who == mTeamID) continue; // skip own team
		status_t status;
		BMessenger app(BEPDF_APP_SIG, who, &status);
		if (status == B_OK) {
			app.SendMessage(&msg, (BHandler*)NULL, 0);
		}
	}
	// notify ourself
	PostMessage(&msg, (BHandler*)NULL, 0);
}

bool BepdfApplication::QuitRequested() {
	delete mStdoutTracer; mStdoutTracer = NULL;
	delete mStderrTracer; mStderrTracer = NULL;

	bool shortcut;
	if (B_OK == CurrentMessage()->FindBool("shortcut", &shortcut) && shortcut) {
		Notify(NOTIFY_QUIT_MSG);
	}
	return BApplication::QuitRequested();
}

///////////////////////////////////////////////////////////
/*
	Opens everything.
*/
void BepdfApplication::RefsReceived(BMessage *msg)
{
	uint32 type;
	int32 count;
	mReadyToQuit = false;

	msg->GetInfo("refs", &type, &count);

	if (type != B_REF_TYPE) {
        BAlert *error = new BAlert(B_TRANSLATE("Error"), B_TRANSLATE("Invalid file reference received!"), B_TRANSLATE("Close"), NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
        error->Go();

        return;
    }

	BString ownerPassword, userPassword;
	const char *owner = NULL;
	const char *user  = NULL;
	BMessage target;
	bool hasTarget = false;
	BString motivation;
	bool mark = false;
	BString annotationId;
	entry_ref ref;

	if (B_OK == msg->FindString("ownerPassword", &ownerPassword)) {
		owner = ownerPassword.String();
	}
	if (B_OK == msg->FindString("userPassword", &userPassword)) {
		user = userPassword.String();
	}

	hasTarget = msg->FindMessage(TARGET_MSG_KEY, &target) == B_OK;
	mark = msg->FindString(MOTIVATION_MSG_KEY, &motivation) == B_OK;
	msg->FindString(ANNOTATION_MSG_KEY, &annotationId);

	Initialize();

	for (int32 i = --count ; i >= 0; i-- ) {
		if ( msg->FindRef("refs", i, &ref ) == B_OK ) {
			/*
				Open the document...
				WARNING: The application thread is used to open a file!
			*/
			PDFWindow *win;
			BRect rect(mSettings->GetWindowRect());
			bool ok;
			bool encrypted = false;

			if (mWindow == NULL) {
				win = new PDFWindow(&ref, rect, owner, user, &encrypted);
				ok = win->IsOk();
			} else {
				win = mWindow;
				win->Lock();
				ok = mWindow->LoadFile(&ref, owner, user, &encrypted);
				win->Unlock();
			}

			if (!ok) {
				if (!encrypted) {
			 		BAlert *error = new BAlert(B_TRANSLATE("Error"), B_TRANSLATE("Toji: Error opening file!"), B_TRANSLATE("Close"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
			 		error->Go();

                    if (mWindow == NULL) {  // fixme: always true even if a PDF window is already open!
                        OpenFilePanel();
                    }
		 		} else {
		 			new PasswordWindow(&ref, rect, this);
                }
		 		if (mWindow == NULL) delete win;

			} else if (mWindow == NULL) {
				mWindow = win;
				win->Show();
			}
            if (annotationId.Length() > 0 && mWindow != NULL) {
                BMessage annotationMsg(PDFWindow::SHOW_ANNOTATION_CMD);
                annotationMsg.AddString("id", annotationId);
                mWindow->PostMessage(&annotationMsg);
            }
            if (hasTarget && mWindow != NULL) {
                BMessage targetMsg(PDFWindow::SHOW_TARGET_CMD);
                targetMsg.AddMessage(TARGET_MSG_KEY, &target);
                if (mark)
                    targetMsg.AddString(MOTIVATION_MSG_KEY, motivation);
                mWindow->PostMessage(&targetMsg);
            }
			// stop after first document
			mGotSomething = true;
			break;
		}
	}
}



///////////////////////////////////////////////////////////
void
BepdfApplication::MessageReceived (BMessage * msg)
{
	if (msg == NULL) {
		fprintf (stderr, "xpdf: message NULL received\n");
		return;
	}

	switch (msg->what) {
	case NOTIFY_QUIT_MSG:
		if (mWindow) {
			BWindow* w = mWindow;
			w->Lock();
			w->PostMessage(B_QUIT_REQUESTED);
			w->Unlock();
		}
		break;
	case NOTIFY_CLOSE_MSG:
		if (mWindow) {
			mWindow->Lock();
			mWindow->UpdateWindowsMenu();
			mWindow->Unlock();
		}
		break;
	case B_CANCEL:
		if (!mWindow && mReadyToQuit) {
			PostMessage(B_QUIT_REQUESTED);
		}
		break;
	default:
		BApplication::MessageReceived(msg);
	}
}





///////////////////////////////////////////////////////////
void
BepdfApplication::ArgvReceived (int32 argc, char **argv)
{
	int pg;
	entry_ref fileToOpen;

	// a link to a place: toji:///path/doc.pdf#page=5 (what another program opens when a toji: link is clicked), or file:
	if (argc == 2 && DeepLink::IsLink(argv[1])) {
		BString path;
		DeepLink::Place place;
		if (DeepLink::Parse(argv[1], &path, &place) && get_ref_for_path(path.String(), &fileToOpen) == B_OK) {
			BMessage msg(B_REFS_RECEIVED);
			msg.AddRef("refs", &fileToOpen);
			BMessage target;
			DeepLink::ToTarget(place, &target);
			if (!target.IsEmpty())
				msg.AddMessage(TARGET_MSG_KEY, &target);
			if (place.annotation.Length() > 0)
				msg.AddString(ANNOTATION_MSG_KEY, WebAnnotation::IdentifierIri(place.annotation.String()));
			PostMessage(&msg);
			mGotSomething = true;
			return;
		}
		fprintf(stderr, "%s: cannot open the link: %s\n", argv[0], argv[1]);
		exit(1);
	}

	// check command line
	if (!(argc == 2 || argc == 3) || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		fprintf(stderr, "usage: %s [<file> [<page>] | <toji: link>]\n", argv[0]);
		exit(1);
	}
	// without a page the document opens where it was left, as it does from Tracker
	pg = argc == 3 ? atoi(argv[2]) : 0;

	BMessage msg(B_REFS_RECEIVED);
	if (pg != 0) {
		// the page as a target, as other programs say it
		BMessage selector, target;
		char value[24];
		snprintf(value, sizeof(value), "page=%d", pg);
		WebAnnotation::MakeFragmentSelector(&selector, WebAnnotation::kConformsToPdf, value);
		target.AddMessage("oa:hasSelector", &selector);
		msg.AddMessage(TARGET_MSG_KEY, &target);
	}
	get_ref_for_path (argv[1], &fileToOpen);
	msg.AddRef ("refs", &fileToOpen);
	PostMessage (&msg);
	mGotSomething = true;
}


///////////////////////////////////////////////////////////
void BepdfApplication::LoadSettings() {
BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK &&
		path.Append(settingsFilename) == B_OK ) {
		// the program was called Tsundoku until version 0.9: its settings are taken over once
		BEntry entry(path.Path());
		if (!entry.Exists()) {
			BPath old;
			if (find_directory(B_USER_SETTINGS_DIRECTORY, &old) == B_OK && old.Append("Tsundoku") == B_OK)
				mSettings->Load(old.Path());
		}
		mSettings->Load(path.Path());
	}
}

///////////////////////////////////////////////////////////
void BepdfApplication::SaveSettings() {
BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK &&
		path.Append(settingsFilename) == B_OK) {
		mSettings->Save(path.Path());
	}
}

static struct {
	const char *name;
	const char *public_name;
	const char *pdf_name;
	int32 type_code;
} gAttrInfo[] = {
	{"dc:description",    "Description", "Subject",      B_STRING_TYPE},
	{"dc:title",          "Title",       "Title",        B_STRING_TYPE},
	{"PDF:creator",       "Creator",     "Creator",      B_STRING_TYPE},
	{"dc:creator",        "Author",      "Author",       B_STRING_TYPE},
	{"dc:subject",        "Keywords",    "Keywords",     B_STRING_TYPE},
	{"PDF:producer",    "Producer",    "Producer",     B_STRING_TYPE},
	{"PDF:created",     "Created",     "CreationDate", B_TIME_TYPE},
	{"PDF:modified",    "Modified",    "ModDate",      B_TIME_TYPE},
	{"schema:numberOfPages", "Pages",     NULL,           B_INT32_TYPE},
	{NULL, NULL, NULL, 0}
};

// A query only finds files by an attribute that is indexed on their volume, so the indices for what Toji writes
// are made (once for a volume) if they are missing.
static void
EnsureIndices(dev_t device)
{
	static const struct { const char* name; uint32 type; } kIndices[] = {
		{ "dc:title", B_STRING_TYPE }, { "dc:creator", B_STRING_TYPE }, { "dc:subject", B_STRING_TYPE },
		{ "PDF:creator", B_STRING_TYPE }, { "schema:numberOfPages", B_INT32_TYPE },
		{ "dc:description", B_STRING_TYPE }, { "dc:publisher", B_STRING_TYPE },
		{ "dc:language", B_STRING_TYPE }, { "dc:identifier", B_STRING_TYPE }, { "schema:isbn", B_STRING_TYPE },
		{ "dcterms:isPartOf", B_STRING_TYPE }, { "schema:position", B_DOUBLE_TYPE },
		{ "SEN:readingProgression", B_STRING_TYPE },
		{ "dc:date", B_INT64_TYPE }, { "SEN:annotationCount", B_INT32_TYPE },
		{ "PDF:created", B_INT64_TYPE }, { "PDF:modified", B_INT64_TYPE }
	};
	static dev_t sDone[16];
	static int sDoneCount = 0;
	for (int i = 0; i < sDoneCount; i++) {
		if (sDone[i] == device)
			return;
	}
	if (sDoneCount < 16)
		sDone[sDoneCount++] = device;

	for (size_t i = 0; i < sizeof(kIndices) / sizeof(kIndices[0]); i++) {
		index_info info;
		if (fs_stat_index(device, kIndices[i].name, &info) != 0)
			fs_create_index(device, kIndices[i].name, kIndices[i].type, 0);
	}
}


///////////////////////////////////////////////////////////
// The attributes that BePDF made up for PDF files (and that this program wrote for books until 0.8), and the standard ones
// that take their place
static const struct { const char* old; const char* standard; } kLegacyAttributes[] = {
	{ "META:title", "dc:title" }, { "META:author", "dc:creator" }, { "META:keyw", "dc:subject" },
	{ "META:pages", "schema:numberOfPages" }, { "META:subject", "dc:description" }, { "META:creator", "PDF:creator" }
};


static bool
HasLegacyAttributes(BNode& node)
{
	attr_info info;
	for (size_t i = 0; i < sizeof(kLegacyAttributes) / sizeof(kLegacyAttributes[0]); i++) {
		if (node.GetAttrInfo(kLegacyAttributes[i].old, &info) == B_OK)
			return true;
	}
	return false;
}


// the value of each is kept (it may have been edited), and the old attribute is taken away
static void
MoveLegacyAttributes(BNode& node)
{
	for (size_t i = 0; i < sizeof(kLegacyAttributes) / sizeof(kLegacyAttributes[0]); i++) {
		attr_info info;
		if (node.GetAttrInfo(kLegacyAttributes[i].old, &info) != B_OK)
			continue;
		bool moved = node.GetAttrInfo(kLegacyAttributes[i].standard, &info) == B_OK;
		if (!moved && node.GetAttrInfo(kLegacyAttributes[i].old, &info) == B_OK && info.size >= 0 && info.size < 65536) {
			char* data = new char[info.size + 1];
			if (node.ReadAttr(kLegacyAttributes[i].old, info.type, 0, data, info.size) == info.size)
				moved = node.WriteAttr(kLegacyAttributes[i].standard, info.type, 0, data, info.size) == info.size;
			delete[] data;
		}
		if (moved)
			node.RemoveAttr(kLegacyAttributes[i].old);
	}
}


///////////////////////////////////////////////////////////
bool
BepdfApplication::FileHasLegacyAttributes(entry_ref* ref)
{
	BNode node(ref);
	if (node.InitCheck() != B_OK)
		return false;
	attr_info info;
	return HasLegacyAttributes(node) || node.GetAttrInfo("bepdf:bookmarks", &info) == B_OK
		|| node.GetAttrInfo("bepdf:page", &info) == B_OK;
}


void
BepdfApplication::ApplyLegacyChoice(entry_ref* ref)
{
	BNode node(ref);
	if (node.InitCheck() == B_OK && gApp->GetSettings()->GetLegacyAttributes() == 2)
		MoveLegacyAttributes(node);
}


///////////////////////////////////////////////////////////
void
BepdfApplication::UpdateAttr(BNode &node, const char *name, type_code type, off_t offset, void *buffer, size_t length) {
	char dummy[10];
	if (B_ENTRY_NOT_FOUND == node.ReadAttr(name, type, offset, (char*)dummy, sizeof(dummy))) {
		node.WriteAttr(name, type, offset, buffer, length);
	}
}


///////////////////////////////////////////////////////////
// the date as far as it is given: 2026, 2026-09 or 2026-09-01
void
BepdfApplication::UpdatePublished(BNode &node, const char *text) {
	int year = 0, month = 1, day = 1;
	if (sscanf(text, "%d-%d-%d", &year, &month, &day) >= 1 && year > 0) {
		struct tm date;
		memset(&date, 0, sizeof(date));
		date.tm_year = year - 1900;
		date.tm_mon = month >= 1 && month <= 12 ? month - 1 : 0;
		date.tm_mday = day >= 1 && day <= 31 ? day : 1;
		date.tm_hour = 12;
		time_t published = mktime(&date);
		if (published != (time_t)-1)
			UpdateAttr(node, "dc:date", B_TIME_TYPE, 0, &published, sizeof(published));
	}
}


///////////////////////////////////////////////////////////
void
BepdfApplication::UpdateFileAttributes(Document *doc, entry_ref *ref) {
	BNode node(ref);
	if (node.InitCheck() != B_OK) return;
	EnsureIndices(ref->device);

	// annotations of other programs get an identifier (and the file is saved)
	GlobalSettings* settings = gApp->GetSettings();
	if (settings->GetUpgradeAnnotationIds())
		doc->UpgradeAnnotationIds();

	// A file that has the attributes of BePDF (and of older versions of this program) gets the standard ones as well. The old
	// ones stay (so that the user can go back), unless the user chose to replace them: then the values are moved.
	if (settings->GetLegacyAttributes() == 2 && HasLegacyAttributes(node))
		MoveLegacyAttributes(node);

	const bool force_overwrite = (modifiers() & B_COMMAND_KEY) == B_COMMAND_KEY;

	if (force_overwrite) {
		for (int i = 0; gAttrInfo[i].name; i++) {
			node.RemoveAttr(gAttrInfo[i].name);
		}
	}

	// The number of pages of a book is what it has in the standard configuration (6 x 9 inches, the default text
	// size): an estimate that stays the same for inventory and citations, like the page count that shops give for
	// an e-book. A book that is read at another text size says nothing about it.
	if (!doc->IsReflowable() || doc->TextSize() == Document::kDefaultTextSize) {
		int32 pages = (int32)doc->PageCount();
		UpdateAttr(node, "schema:numberOfPages", B_INT32_TYPE, 0, &pages, sizeof(int32));
	}

	for (int i = 0; gAttrInfo[i].name; i++) {
		if (gAttrInfo[i].pdf_name == NULL) continue;

		time_t time;
		BString value;
		if (FileInfoWindow::GetProperty(doc, gAttrInfo[i].pdf_name, &value, &time)) {
			if (gAttrInfo[i].type_code == B_TIME_TYPE) {
				if (time != 0) {
					UpdateAttr(node, gAttrInfo[i].name, B_TIME_TYPE, 0, &time, sizeof(time));
				}
			} else {
				UpdateAttr(node, gAttrInfo[i].name, B_STRING_TYPE, 0, (void*)value.String(), value.Length()+1);
			}
		}
	}

	// what only a book says, one attribute for each (so they can be shown in Tracker and queried)
	if (const EpubInfo* epub = doc->Epub()) {
		struct { const char* name; const BString* value; } strings[] = {
			{ "dc:description", &epub->description }, { "dc:publisher", &epub->publisher },
			{ "dc:language", &epub->language }, { "dc:identifier", &epub->identifier },
			{ "schema:isbn", &epub->isbn }, { "dcterms:isPartOf", &epub->series }
		};
		for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); i++) {
			if (strings[i].value->Length() > 0)
				UpdateAttr(node, strings[i].name, B_STRING_TYPE, 0, (void*)strings[i].value->String(),
					strings[i].value->Length() + 1);
		}
		if (epub->seriesIndex.Length() > 0) {
			double position = atof(epub->seriesIndex.String());
			UpdateAttr(node, "schema:position", B_DOUBLE_TYPE, 0, &position, sizeof(position));
		}
		UpdatePublished(node, epub->date.String());
	} else if (const ComicInfo* comic = doc->Comic()) {
		// the comic book managers write ComicInfo.xml, which maps to the same attributes
		struct { const char* name; const BString value; } strings[] = {
			{ "dc:description", comic->summary }, { "dc:publisher", comic->publisher },
			{ "dc:language", comic->language }, { "dcterms:isPartOf", comic->series }
		};
		for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); i++) {
			if (strings[i].value.Length() > 0)
				UpdateAttr(node, strings[i].name, B_STRING_TYPE, 0, (void*)strings[i].value.String(),
					strings[i].value.Length() + 1);
		}
		if (comic->number.Length() > 0) {
			double position = atof(comic->number.String());
			UpdateAttr(node, "schema:position", B_DOUBLE_TYPE, 0, &position, sizeof(position));
		}
		UpdatePublished(node, comic->Date().String());
	}

	// a DjVu file has free metadata: the year (or the date) of the work is its date
	if (doc->IsDjvu()) {
		BString year = doc->Metadata("info:Year");
		if (year.IsEmpty())
			year = doc->Metadata("info:Date");
		if (!year.IsEmpty())
			UpdatePublished(node, year.String());
	}

	// a document that says it is read from the right to the left (a manga, an EPUB) says so in the attribute, so that it
	// can be found; what the reader chose for the file is written when it is closed and is not changed here
	int declared = doc->DeclaredReading();
	if (declared == 1 || declared == 2) {
		attr_info info;
		if (node.GetAttrInfo("SEN:readingProgression", &info) != B_OK)
			UpdateAttr(node, "SEN:readingProgression", B_STRING_TYPE, 0, (void*)(declared == 1 ? "rtl" : "ttb"), 4);
	}

	// how many annotations it has (for a book also the ones that are only in the attribute)
	BPath countPath(ref);
	if (countPath.InitCheck() == B_OK)
		doc->SyncAnnotationCount(countPath.Path());
}


const char* BepdfApplication::GetVersion(BString &version) {
	version = "?.?.?";
	if (be_app == NULL) {
		return version.String();
	}

	app_info info;
	if (be_app->GetAppInfo(&info) != B_OK) {
		return version.String();
	}

	BFile file(&info.ref, B_READ_ONLY);
	if (file.InitCheck() != B_OK) {
		return version.String();
	}

	BAppFileInfo appFileInfo(&file);
	version_info appVersion;
	if (appFileInfo.GetVersionInfo(&appVersion, B_APP_VERSION_KIND) != B_OK) {
		return version.String();
	}

	BString variety = B_TRANSLATE("Unknown");
	switch (appVersion.variety) {
		case 0: variety = B_TRANSLATE("Development");
			break;
		case 1: variety = B_TRANSLATE("Alpha");
			break;
		case 2: variety = B_TRANSLATE("Beta");
			break;
		case 3: variety = B_TRANSLATE("Gamma");
			break;
		case 4: variety = B_TRANSLATE("Golden Master");
			break;
		case 5:
			if (appVersion.internal == 0) {
				// hide variety
				variety = "";
			}
			else {
				variety = B_TRANSLATE("Final");
			}
			break;
	};
	version = "";
	version << appVersion.major << "."
		<< appVersion.middle << "."
		<< appVersion.minor
		<< " " << variety;
	if (appVersion.internal != 0) {
		version << " "<< appVersion.internal;
	}
	return version.String();
}

