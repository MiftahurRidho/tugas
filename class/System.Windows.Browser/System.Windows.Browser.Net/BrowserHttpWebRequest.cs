//
// System.Windows.Browser.Net.BrowserHttpWebRequest class
//
// Contact:
//   Moonlight List (moonlight-list@lists.ximian.com)
//
// Copyright (C) 2007,2009 Novell, Inc (http://www.novell.com)
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

#if NET_2_1

using System;
using System.Diagnostics;
using System.Threading;
using System.Collections;
using System.Globalization;
using System.IO;
using System.Net;
using System.Security;
using System.Windows.Interop;
using System.Runtime.InteropServices;

using Mono;
using Mono.Xaml;

namespace System.Windows.Browser.Net
{
	class BrowserHttpWebRequest : HttpWebRequest
	{
		IntPtr native;
		GCHandle managed;
		IntPtr downloader;
		Uri uri;
		long bytes_read;
		bool aborted;
		bool allow_read_buffering;
		string method = "GET";

		ICrossDomainPolicy policy;
		PolicyAsyncResult policy_async;

		BrowserHttpWebStreamWrapper request;
		BrowserHttpWebResponse response;
		BrowserHttpWebAsyncResult async_result;
		ManualResetEvent wait_handle = new ManualResetEvent (false);
		
		DownloaderResponseStartedDelegate started;
		DownloaderResponseAvailableDelegate available;
		DownloaderResponseFinishedDelegate finished;
 		
		//NOTE: This field name needs to stay in sync with WebRequest_2_1.cs in Systme.Net
 		Delegate progress_delegate;

 		public BrowserHttpWebRequest (Uri uri)
 		{
			started = new DownloaderResponseStartedDelegate (OnAsyncResponseStartedSafe);
			available = new DownloaderResponseAvailableDelegate (OnAsyncDataAvailableSafe);
			finished = new DownloaderResponseFinishedDelegate (OnAsyncResponseFinishedSafe);
 			this.uri = uri;
			managed = GCHandle.Alloc (this, GCHandleType.Normal);
			aborted = false;
			allow_read_buffering = true;
		}

		~BrowserHttpWebRequest ()
		{
			Abort ();

			if (async_result != null)
				async_result.Dispose ();

			if (native == IntPtr.Zero)
				return;

			NativeMethods.downloader_request_free (native);
		}

		public override void Abort ()
		{
			if (native == IntPtr.Zero)
				return;
			
			if (response != null)
				response.InternalAbort ();

			InternalAbort ();
		}

		internal void InternalAbort ()
		{
			aborted = true;
		}

		public override IAsyncResult BeginGetRequestStream (AsyncCallback callback, object state)
		{
			Console.WriteLine ("BrowserHttpWebRequest.BeginGetRequestStream: Should be async, doing it sync for now.");
			BrowserHttpWebAsyncResult result = new BrowserHttpWebAsyncResult (callback, state);
			result.SetComplete ();
			return result;
		}

		internal override IAsyncResult BeginGetResponse (AsyncCallback callback, object state, bool checkPolicy)
		{
			if (checkPolicy) {
				// we're being called to download a policy - still we need a policy to do this
				policy = CrossDomainPolicyManager.PolicyDownloadPolicy;
				async_result = new BrowserHttpWebAsyncResult (callback, state);
				BeginGetResponseInternal ();
				return async_result;
			} else {
				return BeginGetResponse (callback, state);
			}
		}

		public override IAsyncResult BeginGetResponse (AsyncCallback callback, object state)
		{
			// we're not allowed to reuse an aborted request
			if (aborted)
				throw new WebException ("Aborted", WebExceptionStatus.RequestCanceled);

			async_result = new BrowserHttpWebAsyncResult (callback, state);

			// this is a same site (site of origin, SOO) request; or
			// we either already know the policy (previously downloaded); or
			// we try to download the policy

			policy = CrossDomainPolicyManager.GetCachedWebPolicy (uri);
			if (policy == null) {
				// we'll download the policy *then* call BeginGetResponseInternal
				policy_async = (PolicyAsyncResult) CrossDomainPolicyManager.BeginGetPolicy (this, new AsyncCallback (PolicyCallback));
			} else {
				policy_async = null;
				BeginGetResponseInternal ();
			}

			return async_result;
		}

		internal void BeginGetResponseInternal ()
		{
			if (NativeMethods.surface_in_main_thread ()) {
				InitializeNativeRequest (IntPtr.Zero);
			} else {
				TickCallHandler tch = new TickCallHandler (InitializeNativeRequestSafe);

				NativeMethods.time_manager_add_tick_call (NativeMethods.surface_get_time_manager (NativeMethods.plugin_instance_get_surface (PluginHost.Handle)), tch, IntPtr.Zero);

				wait_handle.WaitOne ();

				GC.KeepAlive (tch);
			}
		}

		internal void PolicyCallback (IAsyncResult result)
		{
			policy = CrossDomainPolicyManager.EndGetPolicy (result);
			if (policy == null) {
				// uho, something went wrong (e.g. site down, 404)
				async_result.Exception = new WebException ();
			}
			BeginGetResponseInternal ();
		}

		static uint OnAsyncResponseStartedSafe (IntPtr native, IntPtr context)
		{
			try {
				return OnAsyncResponseStarted (native, context);
			} catch (Exception ex) {
				try {
					Console.WriteLine ("Moonlight: Unhandled exception in BrowserHttpWebRequest.OnAsyncResponseStartedSafe: {0}", ex);
				} catch {
				}
			}
			return 0;
		}
		
		static uint OnAsyncResponseStarted (IntPtr native, IntPtr context)
		{
			GCHandle handle = GCHandle.FromIntPtr (context);
			BrowserHttpWebRequest obj = (BrowserHttpWebRequest) handle.Target;
			
			try {
				obj.bytes_read = 0;
				obj.async_result.Response = new BrowserHttpWebResponse (obj, native);
			} catch (Exception e) {
				obj.async_result.Exception = e;
			}
			return 0;
		}
		
		static uint OnAsyncResponseFinishedSafe (IntPtr native, IntPtr context, bool success, IntPtr data)
		{
			try {
				return OnAsyncResponseFinished (native, context, success, data);
			} catch (Exception ex) {
				try {
					Console.WriteLine ("Moonlight: Unhandled exception in BrowserHttpWebRequest.OnAsyncResponseFinishedSafe: {0}", ex);
				} catch {
				}
			}
			return 0;
		}
		
		static uint OnAsyncResponseFinished (IntPtr native, IntPtr context, bool success, IntPtr data)
		{
			GCHandle handle = GCHandle.FromIntPtr (context);
			BrowserHttpWebRequest obj = (BrowserHttpWebRequest) handle.Target;
			
			try {
				obj.async_result.SetComplete ();
			} catch (Exception e) {
				obj.async_result.Exception = e;
			}
			return 0;
		}
		
		static uint OnAsyncDataAvailableSafe (IntPtr native, IntPtr context, IntPtr data, uint length)
		{
			try {
				return OnAsyncDataAvailable (native, context, data, length);
			} catch (Exception ex) {
				try {
					Console.WriteLine ("Moonlight: Unhandled exception in BrowserHttpWebRequest.OnAsyncDataAvailableSafe: {0}", ex);
				} catch {
				}
			}
			return 0;
		}
		
		static uint OnAsyncDataAvailable (IntPtr native, IntPtr context, IntPtr data, uint length)
		{
			GCHandle handle = GCHandle.FromIntPtr (context);
			BrowserHttpWebRequest obj = (BrowserHttpWebRequest) handle.Target;
			
			try {
				obj.bytes_read += length;
				if (obj.progress_delegate != null)
					obj.progress_delegate.DynamicInvoke (new object[] { obj.bytes_read, obj.async_result.Response.ContentLength, obj.async_result.AsyncState});
			} catch {}

			try {
				// FIXME HACK, Reponse is deleted from FF (see comments in the class) 
				// and can cause a crash if the values are not cached early enough
				obj.async_result.Response.GetStatus ();
				obj.async_result.Response.Write (data, (int) length);
			} catch (Exception e) {
				obj.async_result.Exception = e;
			}
			return 0;
		}

		public override Stream EndGetRequestStream (IAsyncResult asyncResult)
		{
			if (request == null) {
				request = new BrowserHttpWebStreamWrapper (new MemoryStream ());
				request.WriteByte ((byte) '\n');
			}
			return request;
		}

		public override WebResponse EndGetResponse (IAsyncResult asyncResult)
		{
			try {
				CheckProtocolViolation ();

				if (async_result != asyncResult)
					throw new ArgumentException ();

				if (aborted) {
					NativeMethods.downloader_request_abort (native);
					throw new WebException ("Aborted", WebExceptionStatus.RequestCanceled);
				}

				if (policy_async != null) {
					if (!policy_async.IsCompleted) {
						policy_async.AsyncWaitHandle.WaitOne ();
					}
				}

				if (policy == null) {
					// no policy ? then access is not allowed!
					throw new SecurityException ();
				} else if (!policy.IsAllowed (this)) {
					// not allowed by the policy
					throw new SecurityException ();
				}

				if (!async_result.IsCompleted)
					async_result.AsyncWaitHandle.WaitOne ();

				if (async_result.HasException) {
					throw async_result.Exception;
				}

				response = async_result.Response;

				// FIXME redirection
				//			Redirection	Error
				// Normal Request	allowed		throw
				// Policy Request	throw		ignore (no policy)
				if (IsRedirection (response)) {
					if (IsDownloadingPolicy ()) {
						// redirection is NOT allowed for policy files
						throw new SecurityException ("Cannot redirect policy files");
					} else {
						string location = response.Headers ["Location"];
						throw new NotSupportedException ("HTTP redirection to " + location);
					}
				} else if (response.StatusCode != HttpStatusCode.OK) {
					// policy file could be missing, but then it means no policy
					if (!IsDownloadingPolicy ()) {
						throw new WebException ("NotFound", null, WebExceptionStatus.Success, response);
					}
				}
			}
			finally {
				async_result.Dispose ();
				managed.Free ();
			}			
			return response;
		}

		bool IsRedirection (BrowserHttpWebResponse response)
		{
			// FIXME - there's likely a maximum number of redirection allowed because throwing an exception
			switch (response.RealStatusCode) {
			case 302:
				// need to test other cases
				return true;
			default:
				return false;
			}
		}

		bool IsDownloadingPolicy ()
		{
			return (policy == CrossDomainPolicyManager.PolicyDownloadPolicy);
		}

		void InitializeNativeRequestSafe (IntPtr context)
		{
			try {
				InitializeNativeRequest (context);
			} catch (Exception ex) {
				try {
					Console.WriteLine ("Moonlight: Unhandled exception in BrowserHttpWebRequest.InitializeNativeRequestSafe: {0}", ex);
				} catch {
				}
			}
		}
		
		void InitializeNativeRequest (IntPtr context)
		{
			if (native != IntPtr.Zero)
				return;

			downloader = NativeMethods.surface_create_downloader (XamlLoader.SurfaceInDomain);
			if (downloader == IntPtr.Zero)
				throw new NotSupportedException ("Failed to create unmanaged downloader");
			native = NativeMethods.downloader_create_web_request (downloader, method, uri.AbsoluteUri);
			if (native == IntPtr.Zero)
				throw new NotSupportedException ("Failed to create unmanaged WebHttpRequest object.  unsupported browser.");

			if (request != null && request.Length > 1) {
				// this header cannot be set directly inside the collection (hence the helper)
				Headers.SetHeader ("content-length", (request.Length - 1).ToString ());
			}
			
			foreach (string header in Headers.AllKeys)
				NativeMethods.downloader_request_set_http_header (native, header, Headers [header]);

			if (request != null && request.Length > 1) {
				byte [] body = (request.InnerStream as MemoryStream).ToArray ();
				NativeMethods.downloader_request_set_body (native, body, body.Length);
			}
			
			NativeMethods.downloader_request_get_response (native, started, available, finished, GCHandle.ToIntPtr (managed));

			wait_handle.Set ();
		}

		[MonoTODO ("value is unused, current implementation always works like it's true (default)")]
		public override bool AllowReadStreamBuffering {
			get { return allow_read_buffering; }
			set { allow_read_buffering = value; }
		}

		public override bool HaveResponse {
			get {
				if (response != null)
					return true;
				if (async_result != null && async_result.Response != null)
					return true;
				return false;
			}
		}

		public override string Method {
			get { return method; }
			set {
				if (String.IsNullOrEmpty (value))
					throw new NotSupportedException ("Method");

				switch (value.ToUpperInvariant ()) {
				case "GET":
				case "POST":
					method = value;
					break;
				default:
					throw new NotSupportedException ("Method " + value);
				}
			}
		}

		public override Uri RequestUri {
			get { return uri; }
		}

		static string[] bad_get_headers = { "Content-Encoding", "Content-Language", "Content-MD5", "Expires" };

		void CheckProtocolViolation ()
		{
			if (String.Compare (method, "GET", StringComparison.OrdinalIgnoreCase) != 0)
				return;

			// most headers are checked when set, but some are checked much later
			foreach (string header in bad_get_headers) {
				// case insensitive check to internal Headers dictionary
				if (Headers.headers.ContainsKey (header))
					throw new ProtocolViolationException ();
			}
		}
	}
}

#endif
