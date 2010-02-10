//
// BindingExpressionBase.cs
//
// Contact:
//   Moonlight List (moonlight-list@lists.ximian.com)
//
// Copyright 2008 Novell, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
// 
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

using System.Collections;
using System.ComponentModel;
using System.Reflection;
using System.Windows;
using System.Collections.Generic;
using System.Windows.Controls;

using Mono;

namespace System.Windows.Data {

	public abstract class BindingExpressionBase : Expression
	{
		internal bool cached;
		object cachedValue;
		
		bool updatingSource;
		UnmanagedPropertyChangeHandler updateDataSourceCallback;
		
		internal Binding Binding {
			get; private set;
		}
		
		FrameworkElement Target {
			get; set;
		}
		
		internal bool UpdatingSource {
			get { return updatingSource; }
		}
		
		DependencyProperty Property {
			get; set;
		}

		PropertyPathWalker PropertyPathWalker {
			get; set;
		}

		bool TwoWayTextBoxText {
			get { return Target is TextBox && Property == TextBox.TextProperty && Binding.Mode == BindingMode.TwoWay; }
		}

		// This is the object we're databound to
		internal object DataSource {
			get {
				object source = null;
				// There are four possible ways to get the source:
				// Binding.Source, Binding.ElementName, Binding.RelativeSource and finally the fallback to DataContext.
				// Only one of the first three will be non-null
				if (Binding.Source != null) {
					source = Binding.Source;
				} else if (Binding.ElementName != null) {
					source = Target.FindName (Binding.ElementName);
					// FIXME: Not sure if this should be an exception or not. Tests need to be written.
					if (source == null)
						Console.WriteLine ("*** WARNING *** The element referenced in Binding.ElementName could not be found");
				} else if (Binding.RelativeSource != null) {
					if (Binding.RelativeSource.Mode == RelativeSourceMode.Self) {
						source = Target;
					} else if (Binding.RelativeSource.Mode == RelativeSourceMode.TemplatedParent) {
						// FIXME: What happens if there is no template owner?
						source = Target.TemplateOwner;
					} else {
						Console.WriteLine ("*** WARNING *** Unsupported RelativeSourceMode '{0}'", Binding.RelativeSource.Mode);
					}
				}

				// If DataContext is bound, then we need to read the parents datacontext or use null
				if (source == null && Target != null) {
					if (Property == FrameworkElement.DataContextProperty || Property == ContentPresenter.ContentProperty) {
						FrameworkElement e = Target.Parent as FrameworkElement;
						if (e != null) {
							source = e.DataContext;
						}
					} else {
						source = Target.DataContext;
					}
				}
				if (PropertyPathWalker != null)
					PropertyPathWalker.Update (source);
				return source;
			}
		}

		internal BindingExpressionBase (Binding binding, FrameworkElement target, DependencyProperty property)
		{
			Binding = binding;
			Target = target;
			Property = property;

			if (TwoWayTextBoxText)
				((TextBox) target).LostFocus += TextBoxLostFocus;

			if (!string.IsNullOrEmpty (Binding.Path.Path)) {
				PropertyPathWalker = new PropertyPathWalker (Binding.Path.Path);
				if (binding.Mode != BindingMode.OneTime) {
					PropertyPathWalker.ValueChanged += PropertyPathValueChanged;
				}
			}

			if (Binding.Mode == BindingMode.TwoWay && Property is CustomDependencyProperty) {
				updateDataSourceCallback = delegate {
					TryUpdateSourceObject (Target.GetValue (Property));
				};
				Target.AddPropertyChangedHandler (Property, updateDataSourceCallback);
			}
		}

		internal override void Dispose ()
		{
			if (TwoWayTextBoxText)
				((TextBox) Target).LostFocus -= TextBoxLostFocus;
			
			if (updateDataSourceCallback != null)
				Target.RemovePropertyChangedHandler (Property, updateDataSourceCallback);

			if (PropertyPathWalker != null)
				PropertyPathWalker.Update (null);
		}

		internal void Invalidate ()
		{
			cached = false;
			cachedValue = null;
		}
		
		internal override object GetValue (DependencyProperty dp)
		{
			if (cached)
				return cachedValue;

			cached = true;
			if (DataSource == null) {
				cachedValue = dp.DefaultValue;
				return cachedValue;
			}
			else if (string.IsNullOrEmpty (Binding.Path.Path)) {
				// If the path is empty, return the active DataSource
				cachedValue = DataSource;
			}	
			else if (PropertyPathWalker.IsPathBroken) {
				cachedValue = dp.DefaultValue;
				return cachedValue;
			}
			else {
				cachedValue = PropertyPathWalker.Value;
			}
			try {
				cachedValue = ConvertToType (dp, cachedValue);
			} catch {
				cachedValue  = dp.DefaultValue;
			}
			
			return cachedValue;
		}

		object ConvertToType (DependencyProperty dp, object value)
		{
			if (Binding.Converter != null) {
				value = Binding.Converter.Convert (value,
			                           Property.PropertyType,
			                           Binding.ConverterParameter,
			                           Binding.ConverterCulture ?? Helper.DefaultCulture);
			}
			return MoonlightTypeConverter.ConvertObject (dp, value, Target.GetType (), true);
		}

		void TextBoxLostFocus (object sender, RoutedEventArgs e)
		{
			UpdateSourceObject ();
		}

		void PropertyPathValueChanged (object o, EventArgs EventArgs)
		{
			try {
				updatingSource = true;
				Invalidate ();
				object value = PropertyPathWalker.IsPathBroken ? Property.DefaultValue : PropertyPathWalker.Value;
				value = ConvertToType (Property, value);
				Target.SetValueImpl (Property, value);
			} catch {
				//Type conversion exceptions are silently swallowed
			} finally {
				updatingSource = false;
			}
		}

		internal void TryUpdateSourceObject (object value)
		{
			if (Binding.Mode == BindingMode.TwoWay && Binding.UpdateSourceTrigger == UpdateSourceTrigger.Default)
				UpdateSourceObject (value);
		}

		internal void UpdateSourceObject ()
		{
			UpdateSourceObject (Target.GetValue (Property));
		}

		internal void UpdateSourceObject (object value)
		{
			try {
				// TextBox.Text only updates a two way binding if it is *not* focused.
				if (TwoWayTextBoxText && System.Windows.Input.FocusManager.GetFocusedElement () == Target)
					return;
				
				if (updatingSource || PropertyPathWalker.IsPathBroken) {
					return;
				}
				
				var node = PropertyPathWalker.FinalNode;
				if (Binding.Converter != null)
					value = Binding.Converter.ConvertBack (value,
					                                       node.ValueType,
					                                       Binding.ConverterParameter,
					                                       Binding.ConverterCulture ?? Helper.DefaultCulture);
				
				value = MoonlightTypeConverter.ConvertObject (node.PropertyInfo, value, Target.GetType ());
				if (cachedValue == null) {
					if (value == null) {
						return;
					}
				}
				else if (cachedValue.Equals (value)) {
					return;
				}

				updatingSource = true;
				node.SetValue (value);
				cachedValue = value;
			} catch (Exception ex) {
				if (Binding.NotifyOnValidationError && Binding.ValidatesOnExceptions) {
					Target.RaiseBindingValidationError (new ValidationErrorEventArgs (ValidationErrorEventAction.Added, new ValidationError (ex)));
				}
			}
			finally {
				updatingSource = false;
			}
		}
	}
}
