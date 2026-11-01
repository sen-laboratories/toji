/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * @file SenStripeDialog.h
 * @brief The dialog with the dark stripe, as BAlert and BAboutWindow of Haiku have it (header only, Haiku only).
 *
 * The window is a dark band on the left, with a picture (the icon or logo) across its edge, and the content on the light panel on the right. The metrics are
 * those of BAlert (src/kits/interface/Alert.cpp, TAlertView): the stripe is five label spacings wide (for the 32 pixels of an icon; a larger picture gets a wider stripe, see StripeWidth()), the picture is drawn at three spacings from the left, so it
 * sits across the division of the dark and the light part. The picture is not at the top but sits with its centre at a third of the height of the window, so it
 * moves down with a window that is made taller; it never leaves the stripe (a margin, two spacings, to the top and the bottom), so in a window that is just as
 * high as the picture it is level with the first line of the text. The
 * colour of the stripe is a tint of the panel colour (B_DARKEN_1_TINT), so that it follows the colours that people have chosen.
 *
 *   - StripeDialog: the window. Its content area is a vertical layout that any view can be added to; with `quitApplication` the application that was started to
 *     show the dialog (a plugin) quits when it is closed.
 *   - AboutDialog: a StripeDialog with the name, version, description and copyright, and the OK button.
 *   - LoadDialogImage(): a picture of a file, scaled down to the height that it is shown in (a box filter: pictures of a few hundred pixels shown at 128 are
 *     not drawn with the bilinear filter of the app server, which skips most of their pixels).
 *
 *     AboutDialog* about = new AboutDialog("SEN Paperboy", "0.1.0", "What it does", "Copyright ...", LoadDialogImage(path, 128));
 *     about->ShowCentered();
 */

#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <ControlLook.h>
#include <Cursor.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <Messenger.h>
#include <Screen.h>
#include <String.h>
#include <TextView.h>
#include <TranslationUtils.h>
#include <Region.h>
#include <View.h>
#include <Window.h>

#include <algorithm>
#include <math.h>
#include <string.h>

namespace sen {

/**
 * A copy of the picture that is scaled to the height, with the width that keeps its proportions, by averaging the pixels that a new pixel covers.
 * @return a new bitmap (B_RGBA32, opaque if the picture has no transparency), or NULL
 */
inline BBitmap*
ScaleDown(const BBitmap* source, float height)
{
	if (source == NULL || !source->IsValid())
		return NULL;
	color_space space = source->ColorSpace();
	int32 sourceWidth = source->Bounds().IntegerWidth() + 1, sourceHeight = source->Bounds().IntegerHeight() + 1;
	int32 targetHeight = (int32) height;
	if ((space != B_RGB32 && space != B_RGBA32) || targetHeight <= 0 || targetHeight >= sourceHeight) {
		// (not a picture that can be scaled down here, or not bigger than it is shown)
		return new BBitmap(source);
	}
	int32 targetWidth = (int32) floorf(sourceWidth * (float) targetHeight / sourceHeight + 0.5f);
	if (targetWidth < 1)
		targetWidth = 1;

	BBitmap* target = new BBitmap(BRect(0, 0, targetWidth - 1, targetHeight - 1), B_RGBA32);
	if (!target->IsValid()) {
		delete target;
		return NULL;
	}
	const bool hasAlpha = space == B_RGBA32;
	const uint8* from = (const uint8*) source->Bits();
	uint8* to = (uint8*) target->Bits();
	for (int32 y = 0; y < targetHeight; y++) {
		int32 y0 = y * sourceHeight / targetHeight, y1 = (y + 1) * sourceHeight / targetHeight;
		if (y1 <= y0)
			y1 = y0 + 1;
		for (int32 x = 0; x < targetWidth; x++) {
			int32 x0 = x * sourceWidth / targetWidth, x1 = (x + 1) * sourceWidth / targetWidth;
			if (x1 <= x0)
				x1 = x0 + 1;
			uint32 blue = 0, green = 0, red = 0, alpha = 0, count = 0;
			for (int32 sy = y0; sy < y1; sy++) {
				const uint8* pixel = from + sy * source->BytesPerRow() + x0 * 4;
				for (int32 sx = x0; sx < x1; sx++, pixel += 4) {
					blue += pixel[0];
					green += pixel[1];
					red += pixel[2];
					alpha += hasAlpha ? pixel[3] : 255;
					count++;
				}
			}
			uint8* out = to + y * target->BytesPerRow() + x * 4;
			out[0] = blue / count;
			out[1] = green / count;
			out[2] = red / count;
			out[3] = alpha / count;
		}
	}
	return target;
}


/** The picture of a file (JPEG, PNG,... whatever the translators read), scaled down to the height it is shown in; NULL if there is none. */
inline BBitmap*
LoadDialogImage(const char* path, float height)
{
	BBitmap* image = BTranslationUtils::GetBitmap(path);
	if (image == NULL)
		return NULL;
	BBitmap* scaled = ScaleDown(image, height);
	delete image;
	return scaled;
}


/**
 * The distance of the content of the dialog to the edges of the window above, right and below, and of the picture on the stripe to the top: the same, so that the
 * top of the picture and the top of the first line of the text are on one line.
 */
inline float
DialogMargin()
{
	return be_control_look->DefaultLabelSpacing() * 2;
}


/**
 * The width of the dark stripe for a picture of a width (0: none). Five label spacings as BAlert has it, which is right for the icon of 32 pixels: it is a bit more
 * than the three spacings before the picture and half of it. A larger picture (the logo of a program, the cover of a book) gets a wider stripe by the same rule, so
 * that the edge of the stripe is across the middle of the picture, and not near its left edge.
 */
inline float
StripeWidth(float pictureWidth)
{
	const float spacing = be_control_look->DefaultLabelSpacing();
	return std::max(spacing * 5, spacing * 3 + pictureWidth / 2);
}


/**
 * The top of the picture on the stripe: its middle is at a third of the height of the window, but the picture stays on the stripe (a margin to the top and to the
 * bottom). Content that is to line up with the picture (the fields of a form) is centred on the same line: the middle of the picture is the top plus half of its height.
 */
inline float
StripePictureTop(float viewHeight, float pictureHeight)
{
	const float margin = DialogMargin();
	float top = viewHeight / 3 - pictureHeight / 2;
	if (top > viewHeight - margin - pictureHeight)
		top = viewHeight - margin - pictureHeight;
	if (top < margin)
		top = margin;
	return floorf(top);
}


/**
 * The picture at its full size, in a window of its own that lies over the picture on the stripe: the same frame as the small one has. A click on it, Escape, or
 * the window losing the focus (a click anywhere else) closes it.
 */
class EnlargedPictureWindow : public BWindow {
public:
	/** @param center where the middle of the window is (screen), as far as the screen allows it; @param picture the picture, taken over */
	EnlargedPictureWindow(BPoint center, BBitmap* picture)
		:
		BWindow(FrameFor(center, picture), "picture", B_NO_BORDER_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
			B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_ASYNCHRONOUS_CONTROLS)
	{
		AddChild(new View(Bounds(), picture));
	}

	virtual void
	WindowActivated(bool active)
	{
		if (!active)
			PostMessage(B_QUIT_REQUESTED);
	}

private:
	/** the picture is shown at its size, but not larger than most of the screen (then it is scaled down to it) */
	static BRect
	FrameFor(BPoint center, BBitmap*& picture)
	{
		BRect screen = BScreen().Frame();
		const float frame = 2;		// (white and black line around it)
		const float maxHeight = screen.Height() * 0.9f - 2 * frame, maxWidth = screen.Width() * 0.9f - 2 * frame;
		if (picture->Bounds().Height() + 1 > maxHeight) {
			BBitmap* scaled = ScaleDown(picture, maxHeight);
			if (scaled != NULL) {
				delete picture;
				picture = scaled;
			}
		}
		const float width = picture->Bounds().Width() + 1 + 2 * frame, height = picture->Bounds().Height() + 1 + 2 * frame;
		(void) maxWidth;
		BRect rect(0, 0, width - 1, height - 1);
		rect.OffsetTo(floorf(center.x - width / 2), floorf(center.y - height / 2));
		// (on the screen: moved, not cut)
		if (rect.right > screen.right)
			rect.OffsetBy(screen.right - rect.right, 0);
		if (rect.bottom > screen.bottom)
			rect.OffsetBy(0, screen.bottom - rect.bottom);
		if (rect.left < screen.left)
			rect.OffsetBy(screen.left - rect.left, 0);
		if (rect.top < screen.top)
			rect.OffsetBy(0, screen.top - rect.top);
		return rect;
	}

	class View : public BView {
	public:
		View(BRect frame, BBitmap* picture)
			:
			BView(frame, "picture", B_FOLLOW_ALL, B_WILL_DRAW),
			fPicture(picture)
		{
			SetViewColor(B_TRANSPARENT_COLOR);
		}

		virtual ~View() { delete fPicture; }

		virtual void
		AttachedToWindow()
		{
			MakeFocus(true);	// (Escape)
		}

		virtual void
		Draw(BRect)
		{
			BRect bounds = Bounds();
			SetHighColor(0, 0, 0);
			FillRect(bounds);
			SetHighColor(255, 255, 255);
			FillRect(bounds.InsetByCopy(1, 1));
			SetDrawingMode(B_OP_ALPHA);
			SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
			DrawBitmap(fPicture, BPoint(2, 2));
			SetDrawingMode(B_OP_COPY);
		}

		virtual void
		MouseDown(BPoint)
		{
			Window()->PostMessage(B_QUIT_REQUESTED);
		}

		virtual void
		KeyDown(const char* bytes, int32 numBytes)
		{
			if (numBytes == 1 && bytes[0] == B_ESCAPE)
				Window()->PostMessage(B_QUIT_REQUESTED);
			else
				BView::KeyDown(bytes, numBytes);
		}

	private:
		BBitmap*	fPicture;
	};
};


/** The dark stripe with the picture across its edge. */
class StripeView : public BView {
	static const int kShadowDistance = 5;	///< pixels that the shadow is moved to the right and down: it starts below the top edge of the frame and right of its left edge
	static const int kShadowBlur = 2;		///< pixels that it fades out over
	static const uint8 kShadowAlpha = 30;	///< of each of its layers (of 255)

public:
	/**
	 * @param picture the icon or logo, drawn as it is (it is taken over); NULL for a stripe without one
	 * @param full the picture at its full size, if it is larger than it is shown (it is taken over): a click on the picture shows it
	 */
	StripeView(BBitmap* picture, BBitmap* full = NULL)
		:
		BView("stripe", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_FRAME_EVENTS),
		fPicture(picture),
		fFull(full)
	{
		float spacing = be_control_look->DefaultLabelSpacing();
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		// (three spacings from the left to the picture, as TAlertView has it; above and below it the margin of the content, so that the window is never lower than that)
		float width = fPicture != NULL ? spacing * 3 + fPicture->Bounds().Width() + 1 + spacing * 2 : spacing * 5;
		float height = fPicture != NULL ? DialogMargin() + fPicture->Bounds().Height() + 1 + DialogMargin() : 0;
		SetExplicitMinSize(BSize(width, height));
		SetExplicitMaxSize(BSize(width, B_SIZE_UNLIMITED));
		SetExplicitPreferredSize(BSize(width, height));
	}

	virtual ~StripeView()
	{
		delete fPicture;
		delete fFull;
	}

	virtual void
	Draw(BRect)
	{
		float spacing = be_control_look->DefaultLabelSpacing();
		BRect stripe = Bounds();
		stripe.right = StripeWidth(fPicture != NULL ? fPicture->Bounds().Width() + 1 : 0);
		SetHighColor(tint_color(ViewColor(), B_DARKEN_1_TINT));
		FillRect(stripe);
		if (fPicture == NULL)
			return;
		// the centre of the picture at a third of the height, but on the stripe
		BPoint at(spacing * 3, StripePictureTop(Bounds().Height(), fPicture->Bounds().Height() + 1));
		const BRect picture(at.x, at.y, at.x + fPicture->Bounds().Width(), at.y + fPicture->Bounds().Height());

		// a little depth, as a card has that lies on the panel: a soft shadow to the right and below it, and a thin frame (white inside, black outside) around it
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		const BRect frame = picture.InsetByCopy(-2, -2);
		// (the shadow is only outside of the frame: a picture with transparent parts is not darkened by it)
		BRegion outside(Bounds());
		outside.Exclude(frame);
		ConstrainClippingRegion(&outside);
		for (int layer = kShadowBlur; layer >= 0; layer--) {
			// (the layers lie on one another: dark next to the frame, fading out; moved by the distance, they do not show above and left of it)
			SetHighColor(0, 0, 0, kShadowAlpha);
			FillRect(frame.OffsetByCopy(kShadowDistance, kShadowDistance).InsetByCopy(-layer, -layer));
		}
		ConstrainClippingRegion(NULL);
		SetHighColor(0, 0, 0, 255);
		StrokeRect(frame);
		SetHighColor(255, 255, 255, 255);
		StrokeRect(picture.InsetByCopy(-1, -1));
		DrawBitmap(fPicture, at);
		SetDrawingMode(B_OP_COPY);
	}

	/** the picture in the stripe is clickable if there is a larger one */
	virtual void
	MouseDown(BPoint where)
	{
		if (fFull != NULL && PictureFrame().Contains(where))
			ToggleEnlarged();
	}

	virtual void
	MouseMoved(BPoint where, uint32 transit, const BMessage*)
	{
		if (fFull == NULL)
			return;
		BCursor zoom(B_CURSOR_ID_ZOOM_IN);
		if (transit != B_EXITED_VIEW && PictureFrame().Contains(where))
			SetViewCursor(&zoom);
		else
			SetViewCursor(B_CURSOR_SYSTEM_DEFAULT);
	}

	/** The full picture shows, over the picture; a second call (or a click on it, Escape, a click elsewhere) puts it away. */
	void
	ToggleEnlarged()
	{
		if (fFull == NULL)
			return;
		if (fEnlarged.IsValid()) {
			fEnlarged.SendMessage(B_QUIT_REQUESTED);
			fEnlarged = BMessenger();
			return;
		}
		EnlargedPictureWindow* window = new EnlargedPictureWindow(ConvertToScreen(PictureFrame()).LeftTop()
			+ BPoint(PictureFrame().Width() / 2, PictureFrame().Height() / 2), new BBitmap(fFull));
		fEnlarged = BMessenger(window);
		window->Show();
	}

private:
	/** where the picture is: the top left at three spacings from the left, as Draw() puts it */
	BRect
	PictureFrame() const
	{
		if (fPicture == NULL)
			return BRect();
		const float spacing = be_control_look->DefaultLabelSpacing();
		BPoint at(spacing * 3, StripePictureTop(Bounds().Height(), fPicture->Bounds().Height() + 1));
		return BRect(at.x, at.y, at.x + fPicture->Bounds().Width(), at.y + fPicture->Bounds().Height());
	}

	BBitmap*	fPicture;
	BBitmap*	fFull;
	BMessenger	fEnlarged;
};


/** A window with the stripe on the left and a content area on the right. */
class StripeDialog : public BWindow {
public:
	/**
	 * @param title the title of the window
	 * @param picture the icon or logo on the stripe (taken over), NULL for none
	 * @param quitApplication the application quits when the window is closed (a program that has no other window than this one)
	 * @param flags the flags of the window: a fixed size by default, a dialog with a list leaves out B_NOT_RESIZABLE and B_NOT_ZOOMABLE
	 */
	StripeDialog(const char* title, BBitmap* picture, bool quitApplication = false,
			uint32 flags = B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE)
		:
		BWindow(BRect(0, 0, 100, 100), title, B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, flags),
		fContent(NULL),
		fQuitApplication(quitApplication)
	{
		BLayoutBuilder::Group<>(this, B_HORIZONTAL, 0)
			.Add(new StripeView(picture))
			.AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING)
				.GetLayout(&fContent)
				.SetInsets(0, DialogMargin(), DialogMargin(), DialogMargin())
			.End();
	}

	/** The vertical layout on the right of the stripe: add the views of the dialog to it. */
	BGroupLayout* ContentLayout() { return fContent; }

	void
	ShowCentered()
	{
		CenterOnScreen();
		Show();
	}

	virtual bool
	QuitRequested()
	{
		if (fQuitApplication)
			be_app->PostMessage(B_QUIT_REQUESTED);
		return true;
	}

private:
	BGroupLayout*	fContent;
	bool			fQuitApplication;
};


/**
 * The About dialog: the name, the version, the description and the copyright, and OK. A program that has more to offer (a link, a button for support) adds it with
 * AddContent() and AddButton(), and handles the message of its button in its own MessageReceived().
 */
class AboutDialog : public StripeDialog {
public:
	/**
	 * @param name the name of the program (the title of the window is "About <name>")
	 * @param version e.g. "0.1.0"; empty for none
	 * @param description what it is, in a few lines; may be empty
	 * @param copyright the lines of the copyright and the license; may be empty
	 * @param logo the picture for the stripe (taken over), NULL for none
	 * @param quitApplication the application quits when the window is closed
	 * @param title the title of the window, NULL for "About <name>"; a program that is translated gives it, and the two labels, in the language of the user
	 * @param versionLabel the word before the version
	 * @param okLabel the label of the button
	 */
	AboutDialog(const char* name, const char* version, const char* description, const char* copyright, BBitmap* logo,
			bool quitApplication = false, const char* title = NULL, const char* versionLabel = "Version", const char* okLabel = "OK")
		:
		StripeDialog(AboutTitle(name, title).String(), logo, quitApplication),
		fButtons(NULL)
	{
		BString text(name);
		text << "\n";
		int32 nameEnd = text.Length();
		text << "\n";
		if (version != NULL && version[0] != '\0')
			text << versionLabel << " " << version << "\n";
		if (description != NULL && description[0] != '\0')
			text << "\n" << description << "\n";
		if (copyright != NULL && copyright[0] != '\0')
			text << "\n" << copyright;

		float textWidth = be_plain_font->StringWidth("M") * 34;
		BTextView* view = new BTextView(BRect(0, 0, textWidth, 100), "text", BRect(0, 0, textWidth, 100), B_FOLLOW_NONE, B_WILL_DRAW);
		view->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		view->SetWordWrap(true);
		view->MakeEditable(false);
		view->MakeSelectable(false);
		view->SetStylable(true);
		view->SetText(text.String());

		// the name: large and bold (as in BAboutWindow); the empty line after it is just a small gap
		BFont nameFont(be_bold_font);
		nameFont.SetSize(be_plain_font->Size() * 1.6);
		view->SetFontAndColor(0, nameEnd, &nameFont, B_FONT_ALL);
		BFont gapFont(be_plain_font);
		gapFont.SetSize(be_plain_font->Size() * 0.6);
		view->SetFontAndColor(nameEnd, nameEnd + 1, &gapFont, B_FONT_ALL);

		float textHeight = view->TextHeight(0, view->CountLines() - 1);
		view->SetExplicitMinSize(BSize(textWidth, textHeight));
		view->SetExplicitMaxSize(BSize(textWidth, textHeight));

		BButton* ok = new BButton("ok", okLabel, new BMessage(B_QUIT_REQUESTED));
		ok->MakeDefault(true);

		ContentLayout()->AddView(view);
		BLayoutBuilder::Group<>(ContentLayout()).AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING).GetLayout(&fButtons).AddGlue().Add(ok).End();
	}

	/** A view below the text (a link, for example), above the buttons. */
	void
	AddContent(BView* view)
	{
		ContentLayout()->AddView(ContentLayout()->CountItems() - 1, view);
	}

	/** A button left of OK. Its message goes to this window: handle it in MessageReceived() of a subclass. */
	void
	AddButton(const char* label, BMessage* message)
	{
		fButtons->AddView(fButtons->CountItems() - 1, new BButton(label, label, message));
	}

private:
	static BString
	AboutTitle(const char* name, const char* title)
	{
		return title != NULL ? BString(title) : (BString("About ") << name);
	}

	BGroupLayout*	fButtons;
};

}	// namespace sen
