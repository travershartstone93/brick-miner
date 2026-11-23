# Project extraction regression

Run `python3 tests/project/run.py` from brick miner. The test builds a four-translation-unit C++ application using CMake and passes the real compilation database to `mine-project`. One implementation requires a definition from its build command.

The application exercises the following cases.

- Two public functions share one file-local helper. Another translation unit defines its own helper with the same name and signature, as well as two overloads.
- A free function calls a public static method, which calls a private method implemented in another translation unit. The closure reaches both same-named local helpers and a cross-file `void(int&)` helper. The miner keeps the original class declaration and compiles the untouched units with their own flags for the reference client.
- A stateless instance method is independently callable. A second const method takes the integer fields of a plain aggregate as explicit leading arguments. The caller supplies the receiver's offset and then the method argument.
- A method with inaccessible instance state, a live input-stream read, mutable globals, a local mutable static and a call reaching mutable state through another translation unit must have rejection records.

The test deletes the original application source and build before compiling clients from the emitted headers. The standalone clients and the original executable must produce the 63 specified answers, including inputs at both signed 32-bit limits. Relocating the source and reversing compile-command order must preserve the inventory and headers byte for byte. Renaming the operations must change their identities and preserve the expected outputs.
