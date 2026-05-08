These two code modules show how to call managed C# code from legacy unmanaged C/C++ code. The basic technique is that old unmanaged code
loads an old-style DLL written in C++ (using Win32 LoadLibrary) known as a "shim" and every function in the shim maps to a method of the
same name in the managed C#.NET code. 

This allows all the goodness of .NET to be used from old Win32 code.

When passing arguments to/from managed code they have to be converted from plain old types such as null terminated strings to managed types
such as .NET strings. Examples of how to do this are shown.

The two modules show are *example_cpp_dll.cpp* which is the unmanaged C/C++ shim; which when it is called automatically loads and
calls *example_cs_dll.cs* where the .NET functionality lives. 
