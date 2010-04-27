//
// TextPointer.cs
//
// Contact:
//   Moonlight List (moonlight-list@lists.ximian.com)
//
// Copyright (C) 2010 Novell, Inc (http://www.novell.com)
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

using System;

namespace System.Windows.Documents {

	public class TextPointer {

		public bool IsAtInsertionPosition {
			get; private set;
		}

		public LogicalDirection LogicalDirection {
			get; private set;
		}

		public DependencyObject Parent {
			get; private set;
		}

		TextPointer ()
		{
			
		}

		public int CompareTo (TextPointer position)
		{
			throw new NotImplementedException ();
		}

		public Rect GetCharacterRect (LogicalDirection direction)
		{
			throw new NotImplementedException ();
		}

		public TextPointer GetNextInsertionPosition (LogicalDirection direction)
		{
			throw new NotImplementedException ();
		}

		public TextPointer GetPositionAtOffset (int offset, LogicalDirection direction)
		{
			throw new NotImplementedException ();
		}
	}
}
