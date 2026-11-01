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


// Haiku
#include <Alert.h>
#include <locale/Catalog.h>

// BePDF
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#include "CachedPage.h"
#include "PDFView.h"
#include "Thread.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PDFSearch"

///////////////////////////////////////////////////////////

// where a found text starts and ends in page space, taken from the first and last area of the hit
struct FindHit {
	fz_point start, end;
	std::vector<fz_quad> quads;		// all areas of the hit
};

static int
CollectHit(fz_context*, void* data, int numQuads, fz_quad* quads, int, int)
{
	if (numQuads <= 0)
		return 0;

	std::vector<FindHit>* hits = (std::vector<FindHit>*)data;
	const fz_quad& first = quads[0];
	const fz_quad& last = quads[numQuads - 1];
	FindHit hit;
	hit.start = fz_make_point(first.ul.x + 0.5f, (first.ul.y + first.ll.y) / 2);
	hit.end = fz_make_point(last.ur.x - 0.5f, (last.ur.y + last.lr.y) / 2);
	hit.quads.assign(quads, quads + numQuads);
	hits->push_back(hit);
	return 0;
}


// the hits on a page (1-based) in the order of the text
static bool
FindOnPage(Document* document, int pageNo, const char* needle, bool ignoreCase, std::vector<FindHit>* hits)
{
	DocumentLocker locker(document);
	fz_context* context = document->Context();
	fz_page* page = NULL;
	fz_stext_page* text = NULL;
	bool ok = true;

	fz_var(page);
	fz_var(text);
	fz_try(context) {
		page = fz_load_page(context, document->Doc(), pageNo - 1);
		text = fz_new_stext_page_from_page(context, page, NULL);
		fz_match_stext_page_cb(context, text, needle, CollectHit, hits,
			ignoreCase ? FZ_SEARCH_IGNORE_CASE : FZ_SEARCH_EXACT);
	}
	fz_always(context) {
		fz_drop_stext_page(context, text);
		fz_drop_page(context, page);
	}
	fz_catch(context) {
		ok = false;
	}
	return ok;
}


class FindThread : public Thread {
public:
	FindThread(const char *s, bool ignoreCase, bool backward, PDFView* mainView, FindTextWindow *find, bool* stopThread);

	int32 Run();

private:
	bool CanContinue() { return !(*mStopThread); }
	BWindow* Window()  { return mMainView->Window(); }

	void SendPageMsg(int32 page);

	PDFView* mMainView;
	FindTextWindow *mFindWindow;
	BString mFindText;
	bool mCaseSensitive;
	bool mBackward;
	bool* mStopThread;
};

FindThread::FindThread(const char *s, bool ignoreCase, bool backward, PDFView* mainView, FindTextWindow *find, bool* stopThread)
	: Thread("find_thread", B_LOW_PRIORITY)
{
	mFindText = s;
	mCaseSensitive = !ignoreCase;
	mBackward = backward;
	mMainView = mainView;
	mFindWindow = find;
	mStopThread = stopThread;
}

 void FindThread::SendPageMsg(int32 page) {
	BMessage msg(FindTextWindow::FIND_SET_PAGE_MSG);
	msg.AddInt32("page", page);
	mFindWindow->PostMessage(&msg);
}

int32
FindThread::Run() {
	Document* document = mMainView->GetDocument();
	int pages = document->PageCount();
	int startPage = mMainView->Page();

	// continue after the previous hit if the search goes on at the same place
	int index = -1;
	if (mMainView->mFindPage == startPage && mMainView->mFindNeedle == mFindText
		&& mMainView->mFindCaseSensitive == mCaseSensitive)
		index = mMainView->mFindIndex;

	bool found = false;
	int foundPage = 0;
	int foundIndex = -1;
	FindHit hit;
	std::vector<FindHit> hits;

	// the rest of the current page
	if (FindOnPage(document, startPage, mFindText.String(), !mCaseSensitive, &hits)) {
		int count = (int)hits.size();
		int i = mBackward ? (index >= 0 ? index - 1 : count - 1) : index + 1;
		if (i >= 0 && i < count) {
			found = true;
			foundPage = startPage;
			foundIndex = i;
			hit = hits[i];
		}
	}

	// the following (previous) pages, and the current page again from the other end
	for (int step = 1; !found && step <= pages && CanContinue(); step++) {
		int page = mBackward ? startPage - step : startPage + step;
		page = ((page - 1) % pages + pages) % pages + 1;
		SendPageMsg(page);
		hits.clear();
		if (FindOnPage(document, page, mFindText.String(), !mCaseSensitive, &hits) && !hits.empty()) {
			found = true;
			foundPage = page;
			foundIndex = mBackward ? (int)hits.size() - 1 : 0;
			hit = hits[foundIndex];
		}
	}

	if (!found) {
		if (CanContinue()) {
			BAlert *alert = new BAlert("Error", B_TRANSLATE("Search string not found."), B_TRANSLATE("OK"), 0, 0,
				B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->Go();
		}
	} else if (Window()->Lock()) {
		if (foundPage != mMainView->Page()) {
			mMainView->SetPage(foundPage);
			mMainView->WaitForPage();
		}
		mMainView->mFindPage = foundPage;
		mMainView->mFindIndex = foundIndex;
		mMainView->mFindNeedle = mFindText;
		mMainView->mFindCaseSensitive = mCaseSensitive;
		mMainView->mFindHighlight = true;
		mMainView->UpdateFindQuadsOfAll();
		mMainView->SelectFound(hit.start, hit.end);
		Window()->Unlock();
	}

	Window()->PostMessage((uint32)(
		found ?
			FindTextWindow::TEXT_FOUND_NOTIFY_MSG :
			FindTextWindow::TEXT_NOT_FOUND_NOTIFY_MSG));
	return 0;
}

// The words of a quote that a program took from the outline of a document ("2 Related Work") are not always those of the page ("2.  Related Work"): the
// numbering of a heading has a point there, or none, and a gap. These are the other ways to look for them, the likelier first: the numbering with a point,
// then the title alone.
static void
QuoteVariants(const char* quote, std::vector<std::string>* variants, std::string* title)
{
	// blanks in a row are one
	std::string words;
	bool blank = true;
	for (const char* c = quote; *c != '\0'; c++) {
		if (isspace((unsigned char)*c)) {
			blank = true;
			continue;
		}
		if (blank && !words.empty())
			words += ' ';
		blank = false;
		words += *c;
	}

	// the numbering at the start: 2, 2., 3.1, A, A.
	size_t end = 0;
	if (end < words.size() && isdigit((unsigned char)words[end])) {
		while (end < words.size() && (isdigit((unsigned char)words[end])
				|| (words[end] == '.' && end + 1 < words.size() && isdigit((unsigned char)words[end + 1]))))
			end++;
	} else if (end < words.size() && isupper((unsigned char)words[end]) && end + 1 < words.size()
			&& (words[end + 1] == ' ' || words[end + 1] == '.')) {
		end = 1;
	}
	if (end == 0)
		return;

	std::string number = words.substr(0, end);
	size_t rest = end;
	if (rest < words.size() && words[rest] == '.')
		rest++;
	if (rest >= words.size() || words[rest] != ' ')
		return;
	rest++;
	*title = words.substr(rest);
	if (title->size() < 4) {
		title->clear();
		return;
	}
	variants->push_back(number + ". " + *title);
	variants->push_back(*title);
}

///////////////////////////////////////////////////////////
// Shows a passage that was quoted by another application: looks for the text from the page on (the given page
// is where it should be, it is not trusted) and makes it visible: selected, or with mark (the caller asked for a
// highlight) marked with a highlighter for 3 seconds, like the region of a target. No annotation is made, the
// document stays as it is. Runs in the thread of the window.
bool PDFView::ShowQuote(const char* quote, int page, bool mark) {
	if (quote == NULL || quote[0] == '\0' || mDoc == NULL)
		return false;

	int pages = mDoc->PageCount();
	int start = (page >= 1 && page <= pages) ? page : mCurrentPage;

	bool found = false;
	int foundPage = 0;
	FindHit hit;
	for (int step = 0; step < pages && !found; step++) {
		int p = (start - 1 + step) % pages + 1;
		std::vector<FindHit> hits;
		if (FindOnPage(mDoc, p, quote, true, &hits) && !hits.empty()) {
			found = true;
			foundPage = p;
			hit = hits[0];
		}
	}
	if (!found && page >= 1 && page <= pages) {
		// not as it was quoted: as a heading of the page that was named (only there: the title alone is found everywhere)
		std::vector<std::string> variants;
		std::string title;
		QuoteVariants(quote, &variants, &title);
		for (const std::string& variant : variants) {
			std::vector<FindHit> hits;
			if (!FindOnPage(mDoc, page, variant.c_str(), true, &hits) || hits.empty())
				continue;
			// the title alone may be in the text before the heading: the heading is set larger than the text
			size_t best = 0;
			if (variant == title) {
				float bestHeight = 0;
				for (size_t i = 0; i < hits.size(); i++) {
					const fz_quad& q = hits[i].quads[0];
					float height = fabsf(q.ll.y - q.ul.y);
					if (height > bestHeight + 0.5f) {
						bestHeight = height;
						best = i;
					}
				}
			}
			found = true;
			foundPage = page;
			hit = hits[best];
			break;
		}
	}
	if (!found)
		return false;

	if (foundPage != mCurrentPage) {
		SetPage(foundPage);
		WaitForPage();
	}
	ClearFindHighlights();
	if (!mark) {
		SelectFound(hit.start, hit.end);
		return true;
	}

	ClearTargetRegion();
	mTargetPage = foundPage;
	mTargetRegion = fz_empty_rect;
	mTargetQuads = hit.quads;
	FlashTarget();
	// and into view
	// (a rectangle that is made empty would put the origin into the union)
	BRect all;
	for (size_t i = 0; i < hit.quads.size(); i++) {
		BPoint a = mPage->PageToDev(hit.quads[i].ul), b = mPage->PageToDev(hit.quads[i].lr);
		BRect each(fminf(a.x, b.x), fminf(a.y, b.y), fmaxf(a.x, b.x), fmaxf(a.y, b.y));
		all = i == 0 ? each : (all | each);
	}
	BRect shown = all.OffsetByCopy(mLeft, mTop), bounds(Bounds());
	// (the scroll that is remembered: the layout that follows a change of page puts the view back to where it was kept)
	if (all.IsValid() && !bounds.Contains(shown))
		ScrollTo(BPoint(shown.left - 40, shown.top - 60));
	return true;
}

void PDFView::Find(const char *s, bool ignoreCase, bool backward, FindTextWindow *findWindow) {
	mStopFindThread = false;
	FindThread* thread = new FindThread(s, ignoreCase, backward, this, findWindow, &mStopFindThread);
	thread->Resume();
}

void PDFView::StopFind() {
	mStopFindThread = true;
}
