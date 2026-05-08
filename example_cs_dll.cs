//  MODULE:		example_cs_dll.cs
//
//  AUTHOR:		Mark Parker
//
//  PURPOSE:	This is the managed library, written in C# .NET, which is called from
//				unmanaged C code via a CPP mixed-mode shim.
//

using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Net;
using System.Text.RegularExpressions;
using System.Runtime.InteropServices;
using System.Globalization;
using System.Threading;

namespace Bridge
{
	public class ManagedLibrary : IDisposable
	{
		//  Managed Library state
		private bool bDisposed = false;
		enum State { Closed, Open };
		private State eState = State.Closed;

		//  Local items
		private string sError = "";

		//
		//  METHOD:		Dispose
		//
		//  PURPOSE:	Clear down unmanaged and disposable resources
		//
		public void Dispose()
		{
			Dispose(true);
			GC.SuppressFinalize(this);
		}

		protected virtual void Dispose(bool bDisposing)
		{
			if (!bDisposed)
			{
				if (bDisposing)
				{
					// Dispose of retained resources here
				}
			}

			bDisposed = true;
		}

		//
		//  METHOD:		OpenManagedLibrary
		//
		//  PURPOSE:	Opens a connection to the this library
		//
		//  RETURNS:	Effective return is bool, but that is not blittable so returning int instead.
		//
		unsafe public int OpenManagedLibrary(
			byte* sArgMessage,
			int iArgSizMessage)
		{
			bool ok = true;

			try
			{
				//	Put any app specific code here
				//...

				//  Note the managed library is now open
				if (ok)
				{
					eState = State.Open;
				}

				//  On error, output the error string in the message buffer
				if (!ok)
				{
					StringToBytePtr("error message goes here", sArgMessage, iArgSizMessage);
				}
				else
				{
					StringToBytePtr("success message", sArgMessage, iArgSizMessage);
				}
			}
			catch (Exception e)
			{
				StringToBytePtr("exception message here", sArgMessage, iArgSizMessage);
			}

			return ok ? 1 : 0;
		}

		//
		//  METHOD:		CloseManagedLibrary
		//
		//  PURPOSE:	Closes a managedlibrary
		//
		public void CloseManagedLibrary()
		{
			eState = State.Closed;
			return;
		}

		//
		//  METHOD:		GetError
		//
		//  PURPOSE:	Returns the last error as a string
		//
		public string GetError()
		{
			return sError;
		}

		//
		//  METHOD:		ReadData
		//
		//  PURPOSE:	Returns some bytes to the caller
		//
		unsafe public int ReadData(byte* pArgBytes, int iArgNumBytes, int* pArgOutBytes)
		{
			bool ok = true;

			//	Sample data
			string myString = "Hello, World!";
			byte[] byteArray = Encoding.UTF8.GetBytes(myString);

			//	Check state
			if (ok)
			{
				if (eState != State.Open)
				{
					ok = false;
					sError = $"cannot read bytes, wrong state: {(int)eState}"
				}
			}

			//	Return a buffer of data
			if (ok)
			{
				//	Check size
				if (byteArray.Length > iArgNumBytes)
				{
					ok = false;
					sError = "error, output buffer too small";
				}
			}

            // Transfer to the supplied buffer
            if (ok)
			{
				var returnedSize = 0;
				foreach (var b in byteArray)
					pArgBytes[returnedSize++] = b;

				//	And set count
				*pArgOutBytes = (int)byteArray.Length;
			}

			return ok ? 1 : 0;
		}

		//
		//  METHOD:		WriteData
		//
		//  PURPOSE:	Data is passed to the managed library for it to do something with.
		//
		//				P1			Address of bytes
		//				P2			Count of bytes
		//				
		//				Returns		TRUE, success
		//							FALSE, error
		//
		unsafe public int WriteData(byte* argBytes, int argNumBytes)
		{
			bool ok = true;

			//	Check we are in a writing mood
			if (ok)
			{
				if (eState != State.Open)
				{
					ok = false;
					sError = "Bad state";
				}
			}

			//	Do something with the data supplied
			if (ok)
			{
				//...
			}

			//	Done
			return ok ? 1 : 0;
		}

		//
		//  METHOD:		StringToBytePtr
		//
		//  PURPOSE:	Copies a string to a char array, honouring a length limit
		//				by truncation.
		//
		unsafe private void StringToBytePtr(string sArgSource, byte* sArgDest, int iSizDest)
		{
			//	Fill the output with nulls
			for (int i = 0; i < iSizDest; i++)
			{
				sArgDest[i] = 0;
			}

			//	Get the string in ASCII
			byte[] src = Encoding.ASCII.GetBytes(sArgSource);

			//	Copy bytes to the destination array
			int iCharsToCopy = Math.Min(iSizDest - 1, src.Length);

			for (int i = 0; i < iCharsToCopy; i++)
			{
				sArgDest[i] = src[i];
			}

			return;
		}
	}
}