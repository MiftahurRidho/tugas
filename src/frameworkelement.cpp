/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * frameworkelement.cpp: 
 *
 * Copyright 2007 Novell, Inc. (http://www.novell.com)
 *
 * See the LICENSE file included with the distribution for details.
 * 
 */

#include <config.h>

#include <math.h>

#include "debug.h"
#include "geometry.h"
#include "application.h"
#include "deployment.h"
#include "runtime.h"
#include "namescope.h"
#include "frameworkelement.h"
#include "trigger.h"
#include "thickness.h"
#include "collection.h"
#include "style.h"
#include "validators.h"

#define MAX_LAYOUT_PASSES 250

FrameworkElementProvider::FrameworkElementProvider (DependencyObject *obj, PropertyPrecedence precedence) : PropertyValueProvider (obj, precedence)
{
	actual_height_value = NULL;
	actual_width_value = NULL;
	last = Size (-INFINITY, -INFINITY);
}
	
FrameworkElementProvider::~FrameworkElementProvider ()
{
	delete actual_height_value;
	delete actual_width_value;
}
	
Value *
FrameworkElementProvider::GetPropertyValue (DependencyProperty *property)
{
	if (property->GetId () != FrameworkElement::ActualHeightProperty && 
	    property->GetId () != FrameworkElement::ActualWidthProperty)
		return NULL;
	
	FrameworkElement *element = (FrameworkElement *) obj;

	Size actual = element->ComputeActualSize ();

	if (last != actual) {
		last = actual;

		if (actual_height_value)
			delete actual_height_value;
		
		if (actual_width_value)
			delete actual_width_value;
		
		actual_height_value = new Value (actual.height);
		actual_width_value = new Value (actual.width);
	}
	
	if (property->GetId () == FrameworkElement::ActualHeightProperty) {
		return actual_height_value;
	} else {
		return actual_width_value;
	}
};

FrameworkElement::FrameworkElement ()
{
	SetObjectType (Type::FRAMEWORKELEMENT);

	default_style_applied = false;
	get_default_template_cb = NULL;
	measure_cb = NULL;
	arrange_cb = NULL;
	loaded_cb = NULL;
	bounds_with_children = Rect ();
	logical_parent = NULL;

	providers[PropertyPrecedence_LocalStyle] = new StylePropertyValueProvider (this, PropertyPrecedence_LocalStyle);
	providers[PropertyPrecedence_DefaultStyle] = new StylePropertyValueProvider (this, PropertyPrecedence_DefaultStyle);
	providers[PropertyPrecedence_DynamicValue] = new FrameworkElementProvider (this, PropertyPrecedence_DynamicValue);
}

FrameworkElement::~FrameworkElement ()
{
}

void
FrameworkElement::RenderLayoutClip (cairo_t *cr)
{
	FrameworkElement *element = this;
	cairo_matrix_t xform;

	/* store off the current transform since the following loop is about the blow it away */
	cairo_get_matrix (cr, &xform);

	while (element) {
		Geometry *geom = LayoutInformation::GetLayoutClip (element);

		if (geom) {
			geom->Draw (cr);
			cairo_clip (cr);
		}

		// translate by the negative visual offset of the
		// element to get the parent's coordinate space.
		Point *visual_offset = LayoutInformation::GetVisualOffset (element);
		if (visual_offset)
			cairo_translate (cr, -visual_offset->x, -visual_offset->y);

		element = (FrameworkElement*)element->GetVisualParent ();
	}

	/* restore the transform after we're done clipping */
	cairo_set_matrix (cr, &xform);
}

Point
FrameworkElement::GetTransformOrigin ()
{
	Point *user_xform_origin = GetRenderTransformOrigin ();
	
	double width = GetActualWidth ();
	double height = GetActualHeight ();

	return Point (width * user_xform_origin->x, 
		      height * user_xform_origin->y);
}

void
FrameworkElement::SetLogicalParent (DependencyObject *logical_parent, MoonError *error)
{
	if (logical_parent && this->logical_parent && this->logical_parent != logical_parent) {
		MoonError::FillIn (error, MoonError::INVALID_OPERATION, "Element is a child of another element");
		return;
	}			

	this->logical_parent = logical_parent;
}

void
FrameworkElement::ElementAdded (UIElement *item)
{
	UIElement::ElementAdded (item);

		//item->UpdateLayout ();
	/*
	if (IsLayoutContainer () && item->Is (Type::FRAMEWORKELEMENT)) {
		FrameworkElement *fe = (FrameworkElement *)item;
		fe->SetActualWidth (0.0);
		fe->SetActualHeight (0.0);
	} 
	*/
}

void
FrameworkElement::OnPropertyChanged (PropertyChangedEventArgs *args, MoonError *error)
{
	if (args->GetProperty ()->GetOwnerType() != Type::FRAMEWORKELEMENT) {
		UIElement::OnPropertyChanged (args, error);
		return;
	}

	if (args->GetId () == FrameworkElement::WidthProperty ||
	    args->GetId () == FrameworkElement::MaxWidthProperty ||
	    args->GetId () == FrameworkElement::MinWidthProperty ||
	    args->GetId () == FrameworkElement::MaxHeightProperty ||
	    args->GetId () == FrameworkElement::MinHeightProperty ||
	    args->GetId () == FrameworkElement::HeightProperty ||
	    args->GetId () == FrameworkElement::MarginProperty) {

		Point *p = GetRenderTransformOrigin ();

		/* normally we'd only update the bounds of this
		   element on a width/height change, but if the render
		   transform is someplace other than (0,0), the
		   transform needs to be updated as well. */
		FullInvalidate (p->x != 0.0 || p->y != 0.0);
		
		FrameworkElement *visual_parent = (FrameworkElement *)GetVisualParent ();
		if (visual_parent) {
			visual_parent->InvalidateMeasure ();
		}

		InvalidateMeasure ();
		InvalidateArrange ();
		UpdateBounds ();
	}
	else if (args->GetId () == FrameworkElement::StyleProperty) {
		if (args->GetNewValue()) {
			Style *s = args->GetNewValue()->AsStyle ();
			if (s) {
				// this has a side effect of calling
				// ProviderValueChanged on all values
				// in the style, so we might end up
				// with lots of property notifications
				// here (reentrancy ok?)

				Application::GetCurrent()->ApplyStyle (this, s);

				((StylePropertyValueProvider*)providers[PropertyPrecedence_LocalStyle])->SealStyle (s);
			}
		}
	}
	else if (args->GetId () == FrameworkElement::HorizontalAlignmentProperty ||
		 args->GetId () == FrameworkElement::VerticalAlignmentProperty) {
		InvalidateArrange ();
		FullInvalidate (true);
	}

	NotifyListenersOfPropertyChange (args, error);
}

Size
FrameworkElement::ApplySizeConstraints (const Size &size)
{
	Size specified (GetWidth (), GetHeight ());
	Size constrained (GetMinWidth (), GetMinHeight ());
	
	constrained = constrained.Max (size);

	if (!isnan (specified.width))
		constrained.width = specified.width;

	if (!isnan (specified.height))
		constrained.height = specified.height;

	constrained = constrained.Min (GetMaxWidth (), GetMaxHeight ());
	constrained = constrained.Max (GetMinWidth (), GetMinHeight ());
	
	return constrained;
}

void
FrameworkElement::ComputeBounds ()
{
	Size size (GetActualWidth (), GetActualHeight ());
	size = ApplySizeConstraints (size);

	extents = Rect (0, 0, size.width, size.height);

	bounds = IntersectBoundsWithClipPath (extents, false).Transform (&absolute_xform);
	bounds_with_children = bounds;

	VisualTreeWalker walker = VisualTreeWalker (this);
	while (UIElement *item = walker.Step ()) {
		if (!item->GetRenderVisible ())
			continue;

		bounds_with_children = bounds_with_children.Union (item->GetSubtreeBounds ());
	}
}

Rect
FrameworkElement::GetSubtreeBounds ()
{
	VisualTreeWalker walker = VisualTreeWalker (this);
	if (GetSubtreeObject () != NULL) 
		return bounds_with_children;

	return bounds;
}

Size
FrameworkElement::ComputeActualSize ()
{
	UIElement *parent = GetVisualParent ();

	if (GetVisibility () != VisibilityVisible)
		return Size (0.0, 0.0);

	if ((parent && !parent->Is (Type::CANVAS)) || (IsLayoutContainer ())) 
		return GetRenderSize ();

	Size actual (0, 0);

	actual = ApplySizeConstraints (actual);

	return actual;
}

bool
FrameworkElement::InsideLayoutClip (double x, double y)
{
	Geometry *layout_clip = LayoutInformation::GetClip (this);	
	bool inside = true;

	if (!layout_clip)
		return inside;

	TransformPoint (&x, &y);
	inside = layout_clip->GetBounds ().PointInside (x, y);
	layout_clip->unref ();

	return inside;
}

bool
FrameworkElement::InsideObject (cairo_t *cr, double x, double y)
{
	Size framework (GetActualWidth (), GetActualHeight ());
	double nx = x, ny = y;
	
	TransformPoint (&nx, &ny);
	if (nx < 0 || ny < 0 || nx > framework.width || ny > framework.height)
		return false;

	if (!InsideLayoutClip (x, y))
		return false;
	
	return UIElement::InsideObject (cr, x, y);
}

void
FrameworkElement::HitTest (cairo_t *cr, Point p, List *uielement_list)
{
	if (!GetRenderVisible ())
		return;

	if (!GetHitTestVisible ())
		return;
	
	// first a quick bounds check
	if (!GetSubtreeBounds().PointInside (p.x, p.y))
		return;

	/* the clip property is global so we can short out here */
	if (!InsideClip (cr, p.x, p.y))
		return;

	/* create our node and stick it on front */
	List::Node *us = uielement_list->Prepend (new UIElementNode (this));
	bool hit = false;

	VisualTreeWalker walker = VisualTreeWalker (this, ZReverse);
	while (UIElement *child = walker.Step ()) {
		child->HitTest (cr, p, uielement_list);

		if (us != uielement_list->First ()) {
			hit = true;
			break;
		}
	}	

	if (!hit && !InsideObject (cr, p.x, p.y))
		uielement_list->Remove (us);
}

void
FrameworkElement::FindElementsInHostCoordinates (cairo_t *cr, Point host, List *uielement_list)
{
	if (GetVisibility () != VisibilityVisible)
		return;

	if (!GetHitTestVisible ())
		return;
		
	if (bounds_with_children.height <= 0)
		return;

	/* the clip property is global so we can short out here */
	if (!InsideClip (cr, host.x, host.y))
		return;

	cairo_save (cr);

	/* create our node and stick it on front */
	List::Node *us = uielement_list->Prepend (new UIElementNode (this));

	VisualTreeWalker walker = VisualTreeWalker (this, ZForward);
	while (UIElement *child = walker.Step ())
		child->FindElementsInHostCoordinates (cr, host, uielement_list);

	if (us == uielement_list->First ()) {
		cairo_new_path (cr);
		cairo_identity_matrix (cr);

		if (!CanFindElement () || !InsideObject (cr, host.x, host.y))
			uielement_list->Remove (us);
	}
	cairo_restore (cr);
}

// FIXME: This is not the fastest way of implementing this, decomposing the rectangle into
// a series of points is probably going to be quite slow. It's a good first effort.
void
FrameworkElement::FindElementsInHostCoordinates (cairo_t *cr, Rect r, List *uielement_list)
{
	bool res = false;
	if (GetVisibility () != VisibilityVisible)
		return;

	if (!GetHitTestVisible ())
		return;
		
	if (bounds_with_children.height <= 0)
		return;
		
	if (!bounds_with_children.IntersectsWith (r))
		return;
	
	cairo_save (cr);
	cairo_new_path (cr);

	Geometry *clip = GetClip ();
	if (clip) {
		if (!r.IntersectsWith (clip->GetBounds ().Transform (&absolute_xform)))
			return;
		r = r.Intersection (clip->GetBounds ().Transform (&absolute_xform));
	}

	/* create our node and stick it on front */
	List::Node *us = uielement_list->Prepend (new UIElementNode (this));

	VisualTreeWalker walker = VisualTreeWalker (this, ZForward);
	while (UIElement *child = walker.Step ())
		child->FindElementsInHostCoordinates (cr, r, uielement_list);

	if (us == uielement_list->First ()) {
		cairo_new_path (cr);
		cairo_identity_matrix (cr);

		res = false;
		if (CanFindElement ()) {
			res = bounds.Intersection (r) == bounds;
			
			for (int i= r.x; i < (r.x + r.width) && !res; i++)
				for (int j= r.y; j < (r.y + r.height) && !res; j++)
					res = InsideObject (cr, i, j);
		}
		
		if (!res)
			uielement_list->Remove (us);
	}
	cairo_restore (cr);
}

void
FrameworkElement::GetSizeForBrush (cairo_t *cr, double *width, double *height)
{
	*width = GetActualWidth ();
	*height = GetActualHeight ();
}

void
FrameworkElement::Measure (Size availableSize)
{
	//LOG_LAYOUT ("measuring %p %s %g,%g\n", this, GetTypeName (), availableSize.width, availableSize.height);
	ApplyTemplate ();

	Size *last = LayoutInformation::GetPreviousConstraint (this);
	bool domeasure = (this->dirty_flags & DirtyMeasure) > 0;

	domeasure |= !last || last->width != availableSize.width || last->height != availableSize.height;

	if (GetVisibility () != VisibilityVisible) {
		LayoutInformation::SetPreviousConstraint (this, &availableSize);
		SetDesiredSize (Size (0,0));
		return;
	}

	UIElement *parent = GetVisualParent ();
	/* unit tests show a short circuit in this case */
	/*
	if (!parent && !IsContainer () && (!GetSurface () || (GetSurface () && !GetSurface ()->IsTopLevel (this)))) {
		SetDesiredSize (Size (0,0));
		return;
	}
	*/

	if (!domeasure)
		return;

	LayoutInformation::SetPreviousConstraint (this, &availableSize);

	InvalidateArrange ();
	UpdateBounds ();

	dirty_flags &= ~DirtyMeasure;

	Thickness margin = *GetMargin ();
	Size size = availableSize.GrowBy (-margin);

	size = ApplySizeConstraints (size);

	if (measure_cb)
		size = (*measure_cb)(size);
	else
		size = MeasureOverride (size);

	hidden_desire = size;

	if (!parent || parent->Is (Type::CANVAS)) {
		if (Is (Type::CANVAS) || !IsLayoutContainer ()) {
			SetDesiredSize (Size (0,0));
			return;
		}
	}

	// postcondition the results
	size = ApplySizeConstraints (size);

	size = size.GrowBy (margin);
	size = size.Min (availableSize);

	if (GetUseLayoutRounding ()) {
		size.width = round (size.width);
		size.height = round (size.height);
	}
	
	SetDesiredSize (size);
}

Size
FrameworkElement::MeasureOverride (Size availableSize)
{
	Size desired = Size (0,0);

	availableSize = availableSize.Max (desired);

	VisualTreeWalker walker = VisualTreeWalker (this);
	while (UIElement *child = walker.Step ()) {
		child->Measure (availableSize);
		desired = child->GetDesiredSize ();
	}

	return desired.Min (availableSize);
}


// not sure about the disconnect between these two methods..  I would
// imagine both should take Rects and ArrangeOverride would return a
// rectangle as well..
void
FrameworkElement::Arrange (Rect finalRect)
{
	//LOG_LAYOUT ("arranging %p %s %g,%g,%g,%g\n", this, GetTypeName (), finalRect.x, finalRect.y, finalRect.width, finalRect.height);
	Rect *slot = LayoutInformation::GetLayoutSlot (this);
	bool doarrange = this->dirty_flags & DirtyArrange;
	
	doarrange |= slot ? *slot != finalRect : true;

	if (finalRect.width < 0 || finalRect.height < 0 
	    || isinf (finalRect.width) || isinf (finalRect.height)
	    || isnan (finalRect.width) || isnan (finalRect.height)) {
		Size desired = GetDesiredSize ();
		g_warning ("invalid arguments to Arrange (%g,%g,%g,%g) Desired = (%g,%g)", finalRect.x, finalRect.y, finalRect.width, finalRect.height, desired.width, desired.height);
		return;
	}

	UIElement *parent = GetVisualParent ();
	/* unit tests show a short circuit in this case */
	/*
	if (!parent && !IsContainer () && (!GetSurface () || (GetSurface () && !GetSurface ()->IsTopLevel (this)))) {
		return;
	}
	*/
	if (GetVisibility () != VisibilityVisible) {
		LayoutInformation::SetLayoutSlot (this, &finalRect);
		return;
	}

	if (!doarrange)
		return;

	/*
	 * FIXME I'm not happy with doing this here but until I come
	 * up with a better plan make sure that layout elements have
	 * been measured at least once
	 */
	if (IsContainer () && !LayoutInformation::GetPreviousConstraint (this))
		Measure (Size (finalRect.width, finalRect.height));

	ClearValue (LayoutInformation::LayoutClipProperty);

	this->dirty_flags &= ~DirtyArrange;

	Thickness margin = *GetMargin ();
	Rect child_rect = finalRect.GrowBy (-margin);

	cairo_matrix_init_translate (&layout_xform, child_rect.x, child_rect.y);
	UpdateTransform ();
	UpdateBounds ();
	this->dirty_flags &= ~DirtyArrange;

	Size offer = hidden_desire;
	Size response;

	Size framework = ApplySizeConstraints (Size (0, 0));
	Size stretched = ApplySizeConstraints (Size (child_rect.width, child_rect.height));

	HorizontalAlignment horiz = GetHorizontalAlignment ();
	VerticalAlignment vert = GetVerticalAlignment ();

	if (horiz == HorizontalAlignmentStretch) 
		framework.width = MAX (framework.width, stretched.width);

	if (vert == VerticalAlignmentStretch)
		framework.height = MAX (framework.height, stretched.height);

	offer = offer.Max (framework);

	LayoutInformation::SetLayoutSlot (this, &finalRect);

	if (arrange_cb)
		response = (*arrange_cb)(offer);
	else
		response = ArrangeOverride (offer);

	Point visual_offset (child_rect.x, child_rect.y);
	LayoutInformation::SetVisualOffset (this, &visual_offset);

	Size old_size = GetRenderSize ();

	// Kill me Now... 
	response.width = (float) response.width;
	response.height = (float) response.height;

	SetRenderSize (response);

	if (!parent || parent->Is (Type::CANVAS)) {
		if (!IsLayoutContainer ()) {
			SetRenderSize (Size (0,0));
			return;
		}
	}

	Size constrainedResponse = ApplySizeConstraints (response);

	if (GetVisualParent ()) {
		switch (horiz) {
		case HorizontalAlignmentLeft:
			break;
		case HorizontalAlignmentRight:
			visual_offset.x += child_rect.width - constrainedResponse.width;
			break;
		case HorizontalAlignmentCenter:
			visual_offset.x += (child_rect.width - constrainedResponse.width) * .5;
			break;
		default:
			visual_offset.x += MAX ((child_rect.width  - constrainedResponse.width) * .5, 0);
			break;
		}
		
		switch (vert) {
		case VerticalAlignmentTop:
			break;
		case VerticalAlignmentBottom:
			visual_offset.y += child_rect.height - constrainedResponse.height;
			break;
		case VerticalAlignmentCenter:
			visual_offset.y += (child_rect.height - constrainedResponse.height) * .5;
			break;
		default:
			visual_offset.y += MAX ((child_rect.height - constrainedResponse.height) * .5, 0);

			break;
		}
	}

	cairo_matrix_init_translate (&layout_xform, visual_offset.x, visual_offset.y);
	LayoutInformation::SetVisualOffset (this, &visual_offset);

	Rect element (0, 0, response.width, response.height);
	Rect layout_clip = child_rect;
	layout_clip.x = child_rect.x - visual_offset.x;
	layout_clip.y = child_rect.y - visual_offset.y;

	if (((parent && (element != element.Intersection (layout_clip))) || response.Min (constrainedResponse) != response) && !Is (Type::CANVAS) && ((parent && !parent->Is (Type::CANVAS)) || IsContainer ())) {
		layout_clip = element.Intersection (layout_clip);

		RectangleGeometry *rectangle = new RectangleGeometry ();
		rectangle->SetRect (&layout_clip);
		LayoutInformation::SetLayoutClip (this, rectangle);
		rectangle->unref ();
	}

	if (old_size != response) { // || (old_offset && *old_offset != visual_offset)) {
		if (!LayoutInformation::GetLastRenderSize (this))
			LayoutInformation::SetLastRenderSize (this, &old_size);
	}
}

Size
FrameworkElement::ArrangeOverride (Size finalSize)
{
	Size arranged = finalSize;

	VisualTreeWalker walker = VisualTreeWalker (this);
	while (UIElement *child = walker.Step ()) {
		Rect childRect (0,0,finalSize.width,finalSize.height);

		child->Arrange (childRect);
		arranged = arranged.Max (finalSize);
	}

	return arranged;
}

void
FrameworkElement::UpdateLayout ()
{
	UIElement *element = this;
	UIElement *parent = NULL;

	// Seek to the top
	while ((parent = element->GetVisualParent ())) {
		element = parent;
	}

	Surface *surface = element->GetSurface ();

        LOG_LAYOUT ("\nFrameworkElement::UpdateLayout: ");
	List *measure_list = new List ();
	List *arrange_list = new List ();
	List *size_list = new List ();
	bool updated = false;
	int i = 0;
	while (i < MAX_LAYOUT_PASSES) {
		LOG_LAYOUT ("\u267c");
		
		measure_list->Clear (true);
		arrange_list->Clear (true);
		size_list->Clear (true);
		
		i++;
		DeepTreeWalker measure_walker (element);
		while (FrameworkElement *child = (FrameworkElement*)measure_walker.Step ()) {
			if (child->GetVisibility () != VisibilityVisible) {
				measure_walker.SkipBranch ();
				continue;
			}

			if (child->dirty_flags & DirtyMeasure) {
				measure_list->Append (new UIElementNode (child));
			}

			if (!measure_list->IsEmpty ())
				continue;
			
			if (child->dirty_flags & DirtyArrange)
				arrange_list->Append (new UIElementNode (child));
			
			if (!arrange_list->IsEmpty ())
				continue;
			
			if (child->ReadLocalValue (LayoutInformation::LastRenderSizeProperty))
				size_list->Append (new UIElementNode (child));
			
			if (!size_list->IsEmpty ())
				continue;
		}
		
		if (surface)
			surface->needs_measure = !measure_list->IsEmpty ();
		if (!measure_list->IsEmpty ()) {
			while (UIElementNode* node = (UIElementNode*)measure_list->First ()) {
				measure_list->Unlink (node);
				
				node->uielement->DoMeasure ();
				
				updated = true;
				delete (node);
			}
		} else if (!arrange_list->IsEmpty ()) {
			if (surface)
				surface->needs_arrange = false;
			while (UIElementNode *node = (UIElementNode*)arrange_list->First ()) {
				arrange_list->Unlink (node);
				
				if (surface && surface->needs_measure) {
					delete (node);
					break;
				}
				
				node->uielement->DoArrange ();
			
				updated = true;
				delete (node);
			}
		} else if (!size_list->IsEmpty ()) {
			while (UIElementNode *node = (UIElementNode*)size_list->First ()) {
				if (surface && (surface->needs_measure || surface->needs_arrange)) {
					surface->needs_measure = surface->needs_arrange = false;
					break;
				}

				size_list->Unlink (node);
				FrameworkElement *fe = (FrameworkElement*) node->uielement;

				updated = true;
				Size *last = LayoutInformation::GetLastRenderSize (fe);
				if (last) {
					SizeChangedEventArgs *args = new SizeChangedEventArgs (*last, fe->GetRenderSize ());
					fe->ClearValue (LayoutInformation::LastRenderSizeProperty, false);
					fe->Emit (FrameworkElement::SizeChangedEvent, args);
				}
				delete (node);
			}
		} else {
			if (updated)
				Deployment::GetCurrent()->LayoutUpdated ();
			break;
		}
	}
	
	delete measure_list;
	delete arrange_list;
	delete size_list;
	
	if (i >= MAX_LAYOUT_PASSES)  {
		// FIXME we shouldn't have to do this updated call here but otherwise we'll miss it completely
		if (updated)
			Deployment::GetCurrent()->LayoutUpdated ();
		g_warning ("\n************** UpdateLayout Bailing Out after %d Passes *******************\n", i);
	} else {
		LOG_LAYOUT (" (%d)\n", i);
	}
}

void
FrameworkElement::RegisterManagedOverrides (MeasureOverrideCallback measure_cb, ArrangeOverrideCallback arrange_cb,
					    GetDefaultTemplateCallback get_default_template_cb, LoadedCallback loaded_cb)
{
	this->measure_cb = measure_cb;
	this->arrange_cb = arrange_cb;
	this->get_default_template_cb = get_default_template_cb;
	this->loaded_cb = loaded_cb;
}

void
FrameworkElement::SetDefaultStyle (Style *style)
{
	if (style) {
		Application::GetCurrent()->ApplyStyle (this, style);
		default_style_applied = true;
		((StylePropertyValueProvider*)providers[PropertyPrecedence_DefaultStyle])->SealStyle (style);
	}
}


void
FrameworkElement::OnLoaded ()
{
	UIElement::OnLoaded ();

	if (loaded_cb)
		(*loaded_cb) (this);
}

bool
FrameworkElement::ApplyTemplate ()
{
	if (GetSubtreeObject ())
		return false;
	
	bool result = DoApplyTemplate ();
	if (result)
		OnApplyTemplate ();
	return result;
}

bool
FrameworkElement::DoApplyTemplate ()
{
	UIElement *e = GetDefaultTemplate ();
	if (e) {
		MoonError err;
		e->SetParent (this, &err);
		SetSubtreeObject (e);
		ElementAdded (e);
	}
	return e != NULL;
}

void
FrameworkElement::ElementRemoved (UIElement *obj)
{
	if (GetSubtreeObject () == obj) {
		MoonError e;
		obj->SetParent (NULL, &e);
		SetSubtreeObject (NULL);
	}
	UIElement::ElementRemoved (obj);
}

UIElement *
FrameworkElement::GetDefaultTemplate ()
{
	if (get_default_template_cb)
		return get_default_template_cb (this);
	return NULL;
}
