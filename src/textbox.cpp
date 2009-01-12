/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * textbox.cpp: 
 *
 * Contact:
 *   Moonlight List (moonlight-list@lists.ximian.com)
 *
 * Copyright 2007 Novell, Inc. (http://www.novell.com)
 *
 * See the LICENSE file included with the distribution for details.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gdk/gdkkeysyms.h>
#include <cairo.h>

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "dependencyproperty.h"
#include "contentcontrol.h"
#include "textbox.h"
#include "utils.h"


static SolidColorBrush *default_selection_background_brush = NULL;
static SolidColorBrush *default_selection_foreground_brush = NULL;
static SolidColorBrush *default_background_brush = NULL;
static SolidColorBrush *default_foreground_brush = NULL;

static Brush *
default_selection_background (void)
{
	if (!default_selection_background_brush)
		default_selection_background_brush = new SolidColorBrush ("black");
	
	return (Brush *) default_selection_background_brush;
}

static Brush *
default_selection_foreground (void)
{
	if (!default_selection_foreground_brush)
		default_selection_foreground_brush = new SolidColorBrush ("white");
	
	return (Brush *) default_selection_foreground_brush;
}

static Brush *
default_background (void)
{
	if (!default_background_brush)
		default_background_brush = new SolidColorBrush ("white");
	
	return (Brush *) default_background_brush;
}

static Brush *
default_foreground (void)
{
	if (!default_foreground_brush)
		default_foreground_brush = new SolidColorBrush ("black");
	
	return (Brush *) default_foreground_brush;
}

void
textbox_shutdown (void)
{
	if (default_selection_background_brush) {
		default_selection_background_brush->unref ();
		default_selection_background_brush = NULL;
	}
	
	if (default_selection_foreground_brush) {
		default_selection_foreground_brush->unref ();
		default_selection_foreground_brush = NULL;
	}
	
	if (default_background_brush) {
		default_background_brush->unref ();
		default_background_brush = NULL;
	}
	
	if (default_foreground_brush) {
		default_foreground_brush->unref ();
		default_foreground_brush = NULL;
	}
}


//
// TextBuffer
//

#define UNICODE_LEN(size) (sizeof (gunichar) * (size))
#define UNICODE_OFFSET(buf,offset) (((char *) buf) + sizeof (gunichar) * (offset))

class TextBuffer {
	int allocated;
	
	void Resize (int needed)
	{
		bool resize = false;
		
		if (allocated >= needed + 128) {
			while (allocated >= needed + 128)
				allocated -= 128;
			resize = true;
		} else if (allocated < needed) {
			while (allocated < needed)
				allocated += 128;
			resize = true;
		}
		
		if (resize)
			text = (gunichar *) g_realloc (text, UNICODE_LEN (allocated));
	}
	
 public:
	gunichar *text;
	int len;
	
	TextBuffer ()
	{
		text = NULL;
		Reset ();
	}
	
	void Reset ()
	{
		text = (gunichar *) g_realloc (text, UNICODE_LEN (128));
		allocated = 128;
		text[0] = '\0';
		len = 0;
	}
	
	void Append (gunichar c)
	{
		Resize (len + 2);
		
		text[len++] = c;
		text[len] = 0;
	}
	
	void Append (const gunichar *str, int count)
	{
		Resize (len + count + 1);
		
		memcpy (UNICODE_OFFSET (text, len), str, UNICODE_LEN (count));
		len += count;
		text[len] = 0;
	}
	
	void Cut (int start, int length)
	{
		int offset;
		
		if (length == 0 || start >= len)
			return;
		
		offset = MAX (len, start + length);
		
		memmove (UNICODE_OFFSET (text, start), UNICODE_OFFSET (text, offset), UNICODE_LEN ((len - offset) + 1));
	}
	
	void Insert (int index, gunichar c)
	{
		Resize (len + 2);
		
		if (index < len) {
			// shift all chars beyond position @index down by 1 char
			memmove (UNICODE_OFFSET (text, index + 1), UNICODE_OFFSET (text, index), UNICODE_LEN ((len - index) + 1));
			text[index] = c;
			len++;
		} else {
			text[len++] = c;
			text[len] = 0;
		}
	}
	
	void Insert (int index, const gunichar *str, int count)
	{
		Resize (len + count + 1);
		
		if (index < len) {
			// shift all chars beyond position @index down by @count chars
			memmove (UNICODE_OFFSET (text, index + count), UNICODE_OFFSET (text, index), UNICODE_LEN ((len - index) + 1));
			
			// insert @count chars of @str into our buffer at position @index
			memcpy (UNICODE_OFFSET (text, index), str, UNICODE_LEN (count));
			len += count;
		} else {
			// simply append @count chars of @str onto the end of our buffer
			memcpy (UNICODE_OFFSET (text, len), str, UNICODE_LEN (count));
			len += count;
			text[len] = 0;
		}
	}
	
	void Prepend (gunichar c)
	{
		Resize (len + 2);
		
		// shift the entire buffer down by 1 char
		memmove (UNICODE_OFFSET (text, 1), text, UNICODE_LEN (len + 1));
		text[0] = c;
		len++;
	}
	
	void Prepend (const gunichar *str, int count)
	{
		Resize (len + count + 1);
		
		// shift the endtire buffer down by @count chars
		memmove (UNICODE_OFFSET (text, count), text, UNICODE_LEN (len + 1));
		
		// copy @count chars of @str into the beginning of our buffer
		memcpy (text, str, UNICODE_LEN (count));
		len += count;
	}
	
	void Replace (int start, int length, const gunichar *str, int count)
	{
		int beyond;
		
		if (start > len) {
			g_warning ("TextBuffer::Replace() start out of range");
			return;
		}
		
		if (start + length > len) {
			g_warning ("TextBuffer::Replace() length out of range");
			return;
		}
		
		// Check for the easy cases first...
		if (length == 0) {
			Insert (start, str, count);
			return;
		} else if (count == 0) {
			Cut (start, length);
			return;
		} else if (count == length) {
			memcpy (UNICODE_OFFSET (text, start), str, UNICODE_LEN (count));
			return;
		}
		
		Resize ((len - length) + count + 1);
		
		// calculate the number of chars beyond @start that won't be cut
		beyond = len - (start + length);
		
		// shift all chars beyond position (@start + length) into position...
		memmove (UNICODE_OFFSET (text, start + count), UNICODE_OFFSET (text, start + length), UNICODE_LEN (beyond + 1));
		
		// copy @count chars of @str into our buffer at position @start
		memcpy (UNICODE_OFFSET (text, start), str, UNICODE_LEN (count));
		
		len = (len - length) + count;
	}
};


//
// TextBoxDynamicPropertyValueProvider
//

class TextBoxDynamicPropertyValueProvider : public PropertyValueProvider {
	Value *selection_value;
	
 public:
	TextBoxDynamicPropertyValueProvider (DependencyObject *obj) : PropertyValueProvider (obj)
	{
		selection_value = NULL;
	}
	
	virtual ~TextBoxDynamicPropertyValueProvider ()
	{
		delete selection_value;
	}
	
	virtual Value *GetPropertyValue (DependencyProperty *property)
	{
		if (property != TextBox::SelectedTextProperty)
			return NULL;
		
		TextBox *tb = (TextBox *) obj;
		char *selection;
		
		if (tb->selection_changed) {
			delete selection_value;
			selection = g_ucs4_to_utf8 (tb->buffer->text + tb->selection.start, tb->selection.length, NULL, NULL, NULL);
			selection_value = new Value (selection, true);
			tb->selection_changed = false;
		}
		
		return selection_value;
	}
};


//
// TextBox
//

TextBox::TextBox ()
{
	providers[PropertyPrecedence_DynamicValue] = new TextBoxDynamicPropertyValueProvider (this);
	
	AddHandler (UIElement::KeyDownEvent, TextBox::key_down, this);
	AddHandler (UIElement::KeyUpEvent, TextBox::key_up, this);
	
	font = new TextFontDescription ();
	font->SetFamily (CONTROL_FONT_FAMILY);
	font->SetStretch (CONTROL_FONT_STRETCH);
	font->SetWeight (CONTROL_FONT_WEIGHT);
	font->SetStyle (CONTROL_FONT_STYLE);
	font->SetSize (CONTROL_FONT_SIZE);
	
	buffer = new TextBuffer ();
	
	selection.background = default_selection_background ();
	selection.foreground = default_selection_foreground ();
	selection_changed = false;
	selection.length = 0;
	selection.start = 0;
	emit = true;
	maxlen = 0;
	cursor = 0;
}

TextBox::~TextBox ()
{
	RemoveHandler (UIElement::KeyDownEvent, TextBox::key_down, this);
	RemoveHandler (UIElement::KeyUpEvent, TextBox::key_up, this);
	
	delete buffer;
	delete font;
}

#define CURSOR_POSITION_CHANGED (1 << 0)
#define SELECTION_CHANGED       (1 << 1)
#define CONTENT_CHANGED         (1 << 2)
#define NOTHING_CHANGED         (0)

int
TextBox::CursorPageDown (GdkModifierType modifiers)
{
	// FIXME: implement me
	return NOTHING_CHANGED;
}

int
TextBox::CursorPageUp (GdkModifierType modifiers)
{
	// FIXME: implement me
	return NOTHING_CHANGED;
}

int
TextBox::CursorHome (GdkModifierType modifiers)
{
	// FIXME: implement me
	return NOTHING_CHANGED;
}

int
TextBox::CursorEnd (GdkModifierType modifiers)
{
	// FIXME: implement me
	return NOTHING_CHANGED;
}

int
TextBox::CursorRight (GdkModifierType modifiers)
{
	if (modifiers & GDK_SHIFT_MASK) {
		// FIXME: need to handle Ctrl+Shift (selects on word bounds afaict?)
		// change the selection
		if (cursor < buffer->len) {
			if (selection.length > 0) {
				if (cursor > selection.start) {
					// the cursor is at the end of the selection, just grow by 1 char
					SetSelectionLength (selection.length + 1);
				} else {
					// the cursor is at the start of the selection, shrink by 1 char
					if (selection.length > 1) {
						SetSelectionLength (selection.length - 1);
						SetSelectionStart (cursor + 1);
					} else {
						ClearSelection ();
					}
				}
			} else {
				// select the char to the right of the cursor
				SetSelectionStart (cursor);
				SetSelectionLength (1);
			}
			
			cursor++;
			
			return CURSOR_POSITION_CHANGED | SELECTION_CHANGED;
		}
	} else if (selection.length > 0) {
		// clear the selection, place cursor at the end of the selection
		cursor = selection.start + selection.length;
		ClearSelection ();
		
		return CURSOR_POSITION_CHANGED | SELECTION_CHANGED;
	} else if (cursor < buffer->len) {
		// move the cursor one character to the right
		cursor++;
		
		return CURSOR_POSITION_CHANGED;
	}
	
	return NOTHING_CHANGED;
}

int
TextBox::CursorLeft (GdkModifierType modifiers)
{
	if (modifiers & GDK_SHIFT_MASK) {
		// FIXME: need to handle Ctrl+Shift (selects on word bounds afaict?)
		// change the selection
		if (cursor > 0) {
			if (selection.length > 0) {
				if (cursor > selection.start) {
					// the cursor is at the end of the selection, shrink by 1 char
					if (selection.length > 1)
						SetSelectionLength (selection.length - 1);
					else
						ClearSelection ();
				} else {
					// the cursor is at the start of the selection, grow by 1 char
					SetSelectionLength (selection.length + 1);
					SetSelectionStart (cursor - 1);
				}
			} else {
				// select the char to the left of the cursor
				SetSelectionStart (cursor - 1);
				SetSelectionLength (1);
			}
			
			cursor--;
			
			return CURSOR_POSITION_CHANGED | SELECTION_CHANGED;
		}
	} else if (selection.length > 0) {
		// clear the selection, place cursor at the end of the selection
		cursor = selection.start + selection.length;
		ClearSelection ();
		
		return CURSOR_POSITION_CHANGED | SELECTION_CHANGED;
	} else if (cursor > 0) {
		// move the cursor one character to the left
		cursor--;
		
		return CURSOR_POSITION_CHANGED;
	}
	
	return NOTHING_CHANGED;
}

int
TextBox::CursorDown (GdkModifierType modifiers)
{
	// FIXME: implement me
	return NOTHING_CHANGED;
}

int
TextBox::CursorUp (GdkModifierType modifiers)
{
	// FIXME: implement me
	return NOTHING_CHANGED;
}

void
TextBox::OnKeyDown (KeyEventArgs *args)
{
	GdkModifierType modifiers = (GdkModifierType) args->GetModifiers ();
	guint key = args->GetKeyVal ();
	int changed = NOTHING_CHANGED;
	gunichar c;
	
	if (args->IsModifier ())
		return;
	
	// FIXME: so... we'll need to do a few things here:
	// 1. interpret the key (insert a char or move the cursor, etc)
	// 2. update state being mindful of the current state (cursor, selection, etc)
	// 3. register some sort of repeat timeout that expires when OnKeyUp() is called
	//
	// Probably a good idea to lift a lot of the logic from jackson's MWF code.
	printf ("TextBox::OnKeyDown()\n");
	
	if ((c = args->GetUnicode ())) {
		// normal character key
		if ((c == '\n') && !GetAcceptsReturn ())
			return;
		
		if (selection.length > 0) {
			// replace the currently selected text
			buffer->Replace (selection.start, selection.length, &c, 1);
			cursor = selection.start + 1;
			ClearSelection ();
		} else {
			// insert the text at the cursor position
			buffer->Insert (cursor, c);
			cursor++;
		}
		
		changed = CONTENT_CHANGED | CURSOR_POSITION_CHANGED | SELECTION_CHANGED;
	} else {
		switch (key) {
		case GDK_BackSpace:
		case GDK_Delete:
			if (selection.length > 0) {
				// cut the currently selected text
				changed = CONTENT_CHANGED | CURSOR_POSITION_CHANGED | SELECTION_CHANGED;
				buffer->Cut (selection.start, selection.length);
				cursor = selection.start;
				ClearSelection ();
			} else if (key == GDK_BackSpace) {
				if (cursor > 0) {
					// cut the char before the cursor position
					changed = CONTENT_CHANGED | CURSOR_POSITION_CHANGED;
					buffer->Cut (cursor - 1, 1);
					cursor--;
				}
			} else {
				if (buffer->len > cursor) {
					// cut the char after the cursor position
					changed = CONTENT_CHANGED;
					buffer->Cut (cursor, 1);
				}
			}
			break;
		case GDK_KP_Page_Down:
		case GDK_Page_Down:
			changed = CursorPageDown (modifiers);
			break;
		case GDK_KP_Page_Up:
		case GDK_Page_Up:
			changed = CursorPageUp (modifiers);
			break;
		case GDK_KP_Home:
		case GDK_Home:
			changed = CursorHome (modifiers);
			break;
		case GDK_KP_End:
		case GDK_End:
			changed = CursorEnd (modifiers);
			break;
		case GDK_KP_Right:
		case GDK_Right:
			changed = CursorRight (modifiers);
			break;
		case GDK_KP_Left:
		case GDK_Left:
			changed = CursorLeft (modifiers);
			break;
		case GDK_KP_Down:
		case GDK_Down:
			changed = CursorDown (modifiers);
			break;
		case GDK_KP_Up:
		case GDK_Up:
			changed = CursorUp (modifiers);
			break;
		default:
			// FIXME: what other keys do we need to handle?
			break;
		}
	}
	
	if (changed & CONTENT_CHANGED)
		Emit (TextChangedEvent, new TextChangedEventArgs ());
	
	if (changed & SELECTION_CHANGED) {
		// FIXME: need to set the added/remove items?
		Emit (TextChangedEvent, new SelectionChangedEventArgs ());
	}
	
	// only bother emitting this event if the cursor position is
	// the only thing that changed. If either Text or Selection
	// changed, then TextBoxView has already been notified that it
	// needs to invalidate.
	if (changed == CURSOR_POSITION_CHANGED)
		Emit (ModelChangedEvent, new TextBoxModelChangedEventArgs (TextBoxModelChangedCursorPosition));
	
	// FIXME: register a key repeat timeout?
}

void
TextBox::OnKeyUp (KeyEventArgs *args)
{
	// FIXME: unregister the key repeat timeout?
}

void
TextBox::key_down (EventObject *sender, EventArgs *args, void *closure)
{
	((TextBox *) closure)->OnKeyDown ((KeyEventArgs *) args);
}

void
TextBox::key_up (EventObject *sender, EventArgs *args, void *closure)
{
	((TextBox *) closure)->OnKeyUp ((KeyEventArgs *) args);
}

void
TextBox::OnPropertyChanged (PropertyChangedEventArgs *args)
{
	TextBoxModelChangeType changed = TextBoxModelChangedNothing;
	bool invalidate = false;
	
	if (args->property == Control::FontFamilyProperty) {
		char *family = args->new_value ? args->new_value->AsString () : NULL;
		changed = TextBoxModelChangedFont;
		font->SetFamily (family);
	} else if (args->property == Control::FontSizeProperty) {
		double size = args->new_value->AsDouble ();
		changed = TextBoxModelChangedFont;
		font->SetSize (size);
	} else if (args->property == Control::FontStretchProperty) {
		FontStretches stretch = (FontStretches) args->new_value->AsInt32 ();
		changed = TextBoxModelChangedFont;
		font->SetStretch (stretch);
	} else if (args->property == Control::FontStyleProperty) {
		FontStyles style = (FontStyles) args->new_value->AsInt32 ();
		changed = TextBoxModelChangedFont;
		font->SetStyle (style);
	} else if (args->property == Control::FontWeightProperty) {
		FontWeights weight = (FontWeights) args->new_value->AsInt32 ();
		changed = TextBoxModelChangedFont;
		font->SetWeight (weight);
	} else if (args->property == TextBox::AcceptsReturnProperty) {
		// no rendering changes required
	} else if (args->property == TextBox::MaxLengthProperty) {
		// FIXME: What happens if the current buffer length is > MaxLength?
		maxlen = args->new_value->AsInt32 ();
	} else if (args->property == TextBox::SelectedTextProperty) {
		const char *str = args->new_value ? args->new_value->AsString () : "";
		gunichar *text;
		glong textlen;
		
		// FIXME: is the cursor/selection updating logic correct?
		
		text = g_utf8_to_ucs4_fast (str, -1, &textlen);
		if (selection.length > 0) {
			// replace the currently selected text
			buffer->Replace (selection.start, selection.length, text, textlen);
			cursor = selection.start + textlen;
			ClearSelection ();
		} else {
			// insert the text at the cursor position
			buffer->Insert (cursor, text, textlen);
			cursor += textlen;
		}
		
		Emit (TextBox::TextChangedEvent, new TextChangedEventArgs ());
		
		g_free (text);
	} else if (args->property == TextBox::SelectionStartProperty) {
		selection.start = args->new_value->AsInt32 ();
		selection_changed = true;
		
		if (emit)
			Emit (TextBox::SelectionChangedEvent, new SelectionChangedEventArgs ());
	} else if (args->property == TextBox::SelectionLengthProperty) {
		selection.length = args->new_value->AsInt32 ();
		selection_changed = true;
		
		if (emit)
			Emit (TextBox::SelectionChangedEvent, new SelectionChangedEventArgs ());
	} else if (args->property == TextBox::SelectionBackgroundProperty) {
		if (!(selection.background = args->new_value ? args->new_value->AsBrush () : NULL))
			selection.background = default_selection_background ();
		changed = TextBoxModelChangedBrush;
	} else if (args->property == TextBox::SelectionForegroundProperty) {
		if (!(selection.foreground = args->new_value ? args->new_value->AsBrush () : NULL))
			selection.foreground = default_selection_foreground ();
		changed = TextBoxModelChangedBrush;
	} else if (args->property == TextBox::TextProperty) {
		const char *str = args->new_value ? args->new_value->AsString () : "";
		gunichar *text;
		glong textlen;
		
		// FIXME: is the cursor/selection updating logic correct?
		
		text = g_utf8_to_ucs4_fast (str, -1, &textlen);
		buffer->Replace (0, buffer->len, text, textlen);
		ClearSelection ();
		cursor = textlen;
		g_free (text);
		
		Emit (TextBox::TextChangedEvent, new TextChangedEventArgs ());
	} else if (args->property == TextBox::TextAlignmentProperty) {
		changed = TextBoxModelChangedTextAlignment;
	} else if (args->property == TextBox::TextWrappingProperty) {
		changed = TextBoxModelChangedTextWrapping;
	} else if (args->property == TextBox::HorizontalScrollBarVisibilityProperty) {
		/* FIXME: need to figure out:
		 *
		 * 1. whether the scrollbar should be shown or not
		 * 2. whether the layout needs to be recalculated if the visibility changes
		 */
		invalidate = true;
	} else if (args->property == TextBox::VerticalScrollBarVisibilityProperty) {
		/* FIXME: need to figure out:
		 *
		 * 1. whether the scrollbar should be shown or not
		 * 2. whether the layout needs to be recalculated if the visibility changes
		 */
		invalidate = true;
	}
	
	if (invalidate)
		Invalidate ();
	
	if (changed != TextBoxModelChangedNothing)
		Emit (ModelChangedEvent, new TextBoxModelChangedEventArgs (changed, args));
	
	if (args->property->GetOwnerType () != Type::TEXTBOX) {
		Control::OnPropertyChanged (args);
		return;
	}
	
	NotifyListenersOfPropertyChange (args);
}

void
TextBox::OnSubPropertyChanged (DependencyProperty *prop, DependencyObject *obj, PropertyChangedEventArgs *subobj_args)
{
	if (prop == TextBox::SelectionBackgroundProperty ||
	    prop == TextBox::SelectionForegroundProperty ||
	    prop == Control::BackgroundProperty ||
	    prop == Control::ForegroundProperty) {
		Emit (ModelChangedEvent, new TextBoxModelChangedEventArgs (TextBoxModelChangedBrush));
		Invalidate ();
	}
	
	if (prop->GetOwnerType () != Type::TEXTBOX)
		Control::OnSubPropertyChanged (prop, obj, subobj_args);
}

Size
TextBox::ArrangeOverride (Size size)
{
	// FIXME: implement me
	return Control::ArrangeOverride (size);
}

void
TextBox::OnApplyTemplate ()
{
	DependencyObject *content = GetTemplateChild ("ContentElement");
	ContentControl *control;
	TextBoxView *view;
	
	if (content->Is (Type::CONTENTCONTROL)) {
		// Insert our TextBoxView
		control = (ContentControl *) content;
		view = new TextBoxView ();
		view->SetTextBox (this);
		control->SetContent (view);
	}
	
	Control::OnApplyTemplate ();
}

void
TextBox::ClearSelection ()
{
	emit = false;
	SetSelectionLength (0);
	SetSelectionStart (0);
	emit = true;
	
	Emit (TextBox::SelectionChangedEvent, new SelectionChangedEventArgs ());
}

void
TextBox::Select (int start, int length)
{
	if (start < 0 || start > buffer->len || start + length > buffer->len)
		return;
	
	emit = false;
	SetSelectionLength (length);
	SetSelectionStart (start);
	emit = true;
	
	Emit (TextBox::SelectionChangedEvent, new SelectionChangedEventArgs ());
}

void
TextBox::SelectAll ()
{
	Select (0, buffer->len);
}


//
// TextBoxView
//

#define CURSOR_ON_MULTIPLIER 2
#define CURSOR_OFF_MULTIPLIER 1
#define CURSOR_DELAY_MULTIPLIER 3
#define CURSOR_DIVIDER 3

TextBoxView::TextBoxView ()
{
	AddHandler (UIElement::LostFocusEvent, TextBoxView::focus_out, this);
	AddHandler (UIElement::GotFocusEvent, TextBoxView::focus_in, this);
	
	cursor = Rect (0, 0, 0, 0);
	layout = new TextLayout ();
	cursor_visible = false;
	blink_timeout = 0;
	focused = false;
	dirty = false;
}

TextBoxView::~TextBoxView ()
{
	TextBox *textbox = GetTextBox ();
	
	if (textbox)
		textbox->RemoveHandler (TextBox::ModelChangedEvent, TextBoxView::model_changed, this);
	
	RemoveHandler (UIElement::LostFocusEvent, TextBoxView::focus_out, this);
	RemoveHandler (UIElement::GotFocusEvent, TextBoxView::focus_in, this);
	
	if (blink_timeout != 0)
		g_source_remove (blink_timeout);
	
	delete layout;
}

gboolean
TextBoxView::blink (void *user_data)
{
	return ((TextBoxView *) user_data)->Blink ();
}

void
TextBoxView::ConnectBlinkTimeout (guint multiplier)
{
	blink_timeout = g_timeout_add (1000 * multiplier / CURSOR_DIVIDER, TextBoxView::blink, this);
}

bool
TextBoxView::Blink ()
{
	guint multiplier;
	
	if (cursor_visible) {
		multiplier = CURSOR_OFF_MULTIPLIER;
		HideCursor ();
	} else {
		multiplier = CURSOR_ON_MULTIPLIER;
		ShowCursor ();
	}
	
	ConnectBlinkTimeout (multiplier);
	
	return false;
}

void
TextBoxView::DelayCursorBlink ()
{
	TextBox *textbox = GetTextBox ();
	
	if (textbox->GetSelection ()->length == 0) {
		if (blink_timeout != 0)
			g_source_remove (blink_timeout);
		
		ConnectBlinkTimeout (CURSOR_DELAY_MULTIPLIER);
		ShowCursor ();
	}
}

void
TextBoxView::BeginCursorBlink ()
{
	TextBox *textbox = GetTextBox ();
	
	if (textbox->GetSelection ()->length == 0) {
		// no selection, proceed with blinking
		if (blink_timeout == 0) {
			ConnectBlinkTimeout (CURSOR_ON_MULTIPLIER);
			ShowCursor ();
		}
	} else {
		// temporarily disable blinking during selection
		if (blink_timeout != 0) {
			g_source_remove (blink_timeout);
			blink_timeout = 0;
		}
		
		cursor_visible = true;
	}
}

void
TextBoxView::EndCursorBlink ()
{
	if (blink_timeout != 0) {
		g_source_remove (blink_timeout);
		blink_timeout = 0;
	}
	
	if (cursor_visible)
		HideCursor ();
}

void
TextBoxView::ShowCursor ()
{
	// FIXME: we may need to verify that 'cursor' is properly initialized?
	cursor_visible = true;
	Invalidate (cursor);
}

void
TextBoxView::HideCursor ()
{
	// FIXME: we may need to verify that 'cursor' is properly initialized?
	cursor_visible = false;
	Invalidate (cursor);
}

void
TextBoxView::Render (cairo_t *cr, int x, int y, int width, int height)
{
	if (dirty)
		Layout (cr);
	
	cairo_save (cr);
	cairo_set_matrix (cr, &absolute_xform);
	Paint (cr);
	cairo_restore (cr);
}

void
TextBoxView::GetSizeForBrush (cairo_t *cr, double *width, double *height)
{
	*height = GetActualHeight ();
	*width = GetActualWidth ();
}

void
TextBoxView::Layout (cairo_t *cr)
{
	TextBox *textbox = GetTextBox ();
	double width = GetWidth ();
	TextFontDescription *font;
	TextBuffer *buffer;
	List *runs;
	
	if (width > 0.0f)
		layout->SetMaxWidth (width);
	else
		layout->SetMaxWidth (-1.0);
	
	font = textbox->GetFontDescription ();
	buffer = textbox->GetBuffer ();
	
	runs = new List ();
	runs->Append (new TextRun (buffer->text, buffer->len, TextDecorationsNone, font, NULL));
	
	layout->SetTextRuns (runs);
	layout->Layout ();
	
	dirty = false;
}

void
TextBoxView::Paint (cairo_t *cr)
{
	TextBox *textbox = GetTextBox ();
	
	printf ("TextBoxView::Paint()\n");
	
	layout->Render (cr, GetOriginPoint (), Point (), textbox->GetSelection ());
}

void
TextBoxView::selection_changed (EventObject *sender, EventArgs *args, gpointer closure)
{
	((TextBoxView *) closure)->OnSelectionChanged ((SelectionChangedEventArgs *) args);
}

void
TextBoxView::OnSelectionChanged (SelectionChangedEventArgs *args)
{
	// the selected region has changed, need to recalculate layout
	if (focused)
		BeginCursorBlink ();
	
	Invalidate ();
}

void
TextBoxView::text_changed (EventObject *sender, EventArgs *args, gpointer closure)
{
	((TextBoxView *) closure)->OnTextChanged ((TextChangedEventArgs *) args);
}

void
TextBoxView::OnTextChanged (TextChangedEventArgs *args)
{
	// text has changed, need to recalculate layout
	if (focused)
		DelayCursorBlink ();
	
	dirty = true;
	
	UpdateBounds (true);
	Invalidate ();
}

void
TextBoxView::model_changed (EventObject *sender, EventArgs *args, gpointer closure)
{
	((TextBoxView *) closure)->OnModelChanged ((TextBoxModelChangedEventArgs *) args);
}

void
TextBoxView::OnModelChanged (TextBoxModelChangedEventArgs *args)
{
	switch (args->changed) {
	case TextBoxModelChangedCursorPosition:
		// cursor position changed, just need to re-render
		if (focused)
			DelayCursorBlink ();
		break;
	case TextBoxModelChangedTextAlignment:
		// text alignment changed, update our layout
		dirty = layout->SetTextAlignment ((TextAlignment) args->property->new_value->AsInt32 ());
		break;
	case TextBoxModelChangedTextWrapping:
		// text wrapping changed, update our layout
		dirty = layout->SetTextWrapping ((TextWrapping) args->property->new_value->AsInt32 ());
		break;
	case TextBoxModelChangedBrush:
		// a brush has changed, no layout updates needed, we just need to re-render
		break;
	case TextBoxModelChangedFont:
		// font changed, need to recalculate layout
		dirty = true;
		break;
	default:
		// nothing changed??
		return;
	}
	
	if (dirty)
		UpdateBounds (true);
	
	Invalidate ();
}

void
TextBoxView::focus_out (EventObject *sender, EventArgs *args, gpointer closure)
{
	((TextBoxView *) closure)->OnFocusOut (args);
}

void
TextBoxView::OnFocusOut (EventArgs *args)
{
	EndCursorBlink ();
	focused = false;
}

void
TextBoxView::focus_in (EventObject *sender, EventArgs *args, gpointer closure)
{
	((TextBoxView *) closure)->OnFocusIn (args);
}

void
TextBoxView::OnFocusIn (EventArgs *args)
{
	BeginCursorBlink ();
	focused = true;
}

void
TextBoxView::OnPropertyChanged (PropertyChangedEventArgs *args)
{
	TextBox *textbox;
	
	if (args->property == TextBoxView::TextBoxProperty) {
		// remove the event handler from the old textbox
		if ((textbox = args->old_value ? args->old_value->AsTextBox () : NULL)) {
			textbox->RemoveHandler (TextBox::SelectionChangedEvent, TextBoxView::selection_changed, this);
			textbox->RemoveHandler (TextBox::ModelChangedEvent, TextBoxView::model_changed, this);
			textbox->RemoveHandler (TextBox::TextChangedEvent, TextBoxView::text_changed, this);
		}
		
		// add the event handler to the new textbox
		if ((textbox = args->new_value ? args->new_value->AsTextBox () : NULL)) {
			textbox->AddHandler (TextBox::SelectionChangedEvent, TextBoxView::selection_changed, this);
			textbox->AddHandler (TextBox::ModelChangedEvent, TextBoxView::model_changed, this);
			textbox->AddHandler (TextBox::TextChangedEvent, TextBoxView::text_changed, this);
			
			// sync our layout hints state
			layout->SetTextAlignment (textbox->GetTextAlignment ());
			layout->SetTextWrapping (textbox->GetTextWrapping ());
		}
		
		UpdateBounds (true);
		Invalidate ();
		dirty = true;
	}
	
	if (args->property->GetOwnerType () != Type::TEXTBOXVIEW) {
		FrameworkElement::OnPropertyChanged (args);
		
		if (args->property == FrameworkElement::WidthProperty) {
			if (layout->GetTextWrapping () != TextWrappingNoWrap)
				dirty = true;
			
			UpdateBounds (true);
		}
		
		return;
	}
	
	NotifyListenersOfPropertyChange (args);
}

Size
TextBoxView::ArrangeOverride (Size size)
{
	// FIXME: do we want to override this method? We probably do...
	return FrameworkElement::ArrangeOverride (size);
}


//
// PasswordBox
//

void
PasswordBox::OnPropertyChanged (PropertyChangedEventArgs *args)
{
	if (args->property == PasswordBox::PasswordProperty)
		Emit (PasswordBox::PasswordChangedEvent);
	
	TextBox::OnPropertyChanged (args);	
}
