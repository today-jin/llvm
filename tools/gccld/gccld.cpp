//===- gccld.cpp - LLVM 'ld' compatible linker ----------------------------===//
//
// This utility is intended to be compatible with GCC, and follows standard
// system 'ld' conventions.  As such, the default output file is ./a.out.
// Additionally, this program outputs a shell script that is used to invoke LLI
// to execute the program.  In this manner, the generated executable (a.out for
// example), is directly executable, whereas the bytecode file actually lives in
// the a.out.bc file generated by this program.  Also, Force is on by default.
//
// Note that if someone (or a script) deletes the executable program generated,
// the .bc file will be left around.  Considering that this is a temporary hack,
// I'm not too worried about this.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/Linker.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Bytecode/Reader.h"
#include "llvm/Bytecode/WriteBytecodePass.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"
#include "Support/FileUtilities.h"
#include "Support/CommandLine.h"
#include "Support/Signals.h"
#include "Config/unistd.h"
#include <fstream>
#include <memory>
#include <set>
#include <algorithm>

namespace {
  cl::list<std::string> 
  InputFilenames(cl::Positional, cl::desc("<input bytecode files>"),
                 cl::OneOrMore);

  cl::opt<std::string> 
  OutputFilename("o", cl::desc("Override output filename"), cl::init("a.out"),
                 cl::value_desc("filename"));

  cl::opt<bool>    
  Verbose("v", cl::desc("Print information about actions taken"));
  
  cl::list<std::string> 
  LibPaths("L", cl::desc("Specify a library search path"), cl::Prefix,
           cl::value_desc("directory"));

  cl::list<std::string> 
  Libraries("l", cl::desc("Specify libraries to link to"), cl::Prefix,
            cl::value_desc("library prefix"));

  cl::opt<bool>
  Strip("s", cl::desc("Strip symbol info from executable"));

  cl::opt<bool>
  NoInternalize("disable-internalize",
                cl::desc("Do not mark all symbols as internal"));
  static cl::alias
  ExportDynamic("export-dynamic", cl::desc("Alias for -disable-internalize"),
                cl::aliasopt(NoInternalize));

  cl::opt<bool>
  LinkAsLibrary("link-as-library", cl::desc("Link the .bc files together as a"
                                            " library, not an executable"));

  // Compatibility options that are ignored, but support by LD
  cl::opt<std::string>
  CO3("soname", cl::Hidden, cl::desc("Compatibility option: ignored"));
  cl::opt<std::string>
  CO4("version-script", cl::Hidden, cl::desc("Compatibility option: ignored"));
  cl::opt<bool>
  CO5("eh-frame-hdr", cl::Hidden, cl::desc("Compatibility option: ignored"));
  cl::opt<bool>
  CO6("r", cl::Hidden, cl::desc("Compatibility option: ignored"));
}

// FileExists - Return true if the specified string is an openable file...
static inline bool FileExists(const std::string &FN) {
  return access(FN.c_str(), F_OK) != -1;
}


// LoadObject - Read the specified "object file", which should not search the
// library path to find it.
static inline std::auto_ptr<Module> LoadObject(std::string FN,
                                               std::string &OutErrorMessage) {
  if (Verbose) std::cerr << "Loading '" << FN << "'\n";
  if (!FileExists(FN)) {
    // Attempt to load from the LLVM_LIB_SEARCH_PATH directory... if we would
    // otherwise fail.  This is used to locate objects like crtend.o.
    //
    char *SearchPath = getenv("LLVM_LIB_SEARCH_PATH");
    if (SearchPath && FileExists(std::string(SearchPath)+"/"+FN))
      FN = std::string(SearchPath)+"/"+FN;
    else {
      OutErrorMessage = "could not find input file '" + FN + "'!";
      return std::auto_ptr<Module>();
    }
  }

  std::string ErrorMessage;
  Module *Result = ParseBytecodeFile(FN, &ErrorMessage);
  if (Result) return std::auto_ptr<Module>(Result);

  OutErrorMessage = "Bytecode file '" + FN + "' corrupt!";
  if (ErrorMessage.size()) OutErrorMessage += ": " + ErrorMessage;
  return std::auto_ptr<Module>();
}


static Module *LoadSingleLibraryObject(const std::string &Filename) {
  std::string ErrorMessage;
  std::auto_ptr<Module> M = LoadObject(Filename, ErrorMessage);
  if (M.get() == 0 && Verbose) {
    std::cerr << "Error loading '" + Filename + "'";
    if (!ErrorMessage.empty()) std::cerr << ": " << ErrorMessage;
    std::cerr << "\n";
  }
  
  return M.release();
}

// IsArchive -  Returns true iff FILENAME appears to be the name of an ar
// archive file. It determines this by checking the magic string at the
// beginning of the file.
static bool IsArchive(const std::string &filename) {
  std::string ArchiveMagic("!<arch>\012");
  char buf[1 + ArchiveMagic.size()];
  std::ifstream f(filename.c_str());
  f.read(buf, ArchiveMagic.size());
  buf[ArchiveMagic.size()] = '\0';
  return ArchiveMagic == buf;
}

// LoadLibraryExactName - This looks for a file with a known name and tries to
// load it, similarly to LoadLibraryFromDirectory(). 
static inline bool LoadLibraryExactName(const std::string &FileName,
    std::vector<Module*> &Objects, bool &isArchive) {
  if (Verbose) std::cerr << "  Considering '" << FileName << "'\n";
  if (FileExists(FileName)) {
	if (IsArchive(FileName)) {
      std::string ErrorMessage;
      if (Verbose) std::cerr << "  Loading '" << FileName << "'\n";
      if (!ReadArchiveFile(FileName, Objects, &ErrorMessage)) {
        isArchive = true;
        return false;           // Success!
      }
      if (Verbose) {
        std::cerr << "  Error loading archive '" + FileName + "'";
        if (!ErrorMessage.empty()) std::cerr << ": " << ErrorMessage;
        std::cerr << "\n";
      }
    } else {
      if (Module *M = LoadSingleLibraryObject(FileName)) {
        isArchive = false;
        Objects.push_back(M);
        return false;
      }
    }
  }
  return true;
}

// LoadLibrary - Try to load a library named LIBNAME that contains
// LLVM bytecode. If SEARCH is true, then search for a file named
// libLIBNAME.{a,so,bc} in the current library search path.  Otherwise,
// assume LIBNAME is the real name of the library file.  This method puts
// the loaded modules into the Objects list, and sets isArchive to true if
// a .a file was loaded. It returns true if no library is found or if an
// error occurs; otherwise it returns false.
//
static inline bool LoadLibrary(const std::string &LibName,
                               std::vector<Module*> &Objects, bool &isArchive,
                               bool search, std::string &ErrorMessage) {
  if (search) {
    // First, try the current directory. Then, iterate over the
    // directories in LibPaths, looking for a suitable match for LibName
    // in each one.
    for (unsigned NextLibPathIdx = 0; NextLibPathIdx != LibPaths.size();
         ++NextLibPathIdx) {
      std::string Directory = LibPaths[NextLibPathIdx] + "/";
      if (!LoadLibraryExactName(Directory + "lib" + LibName + ".a",
        Objects, isArchive))
          return false;
      if (!LoadLibraryExactName(Directory + "lib" + LibName + ".so",
        Objects, isArchive))
          return false;
      if (!LoadLibraryExactName(Directory + "lib" + LibName + ".bc",
        Objects, isArchive))
          return false;
    }
  } else {
    // If they said no searching, then assume LibName is the real name.
    if (!LoadLibraryExactName(LibName, Objects, isArchive))
      return false;
  }
  ErrorMessage = "error linking library '-l" + LibName+ "': library not found!";
  return true;
}

static void GetAllDefinedSymbols(Module *M, 
                                 std::set<std::string> &DefinedSymbols) {
  for (Module::iterator I = M->begin(), E = M->end(); I != E; ++I)
    if (I->hasName() && !I->isExternal() && !I->hasInternalLinkage())
      DefinedSymbols.insert(I->getName());
  for (Module::giterator I = M->gbegin(), E = M->gend(); I != E; ++I)
    if (I->hasName() && !I->isExternal() && !I->hasInternalLinkage())
      DefinedSymbols.insert(I->getName());
}

// GetAllUndefinedSymbols - This calculates the set of undefined symbols that
// still exist in an LLVM module.  This is a bit tricky because there may be two
// symbols with the same name, but different LLVM types that will be resolved to
// each other, but aren't currently (thus we need to treat it as resolved).
//
static void GetAllUndefinedSymbols(Module *M, 
                                   std::set<std::string> &UndefinedSymbols) {
  std::set<std::string> DefinedSymbols;
  UndefinedSymbols.clear();   // Start out empty
  
  for (Module::iterator I = M->begin(), E = M->end(); I != E; ++I)
    if (I->hasName()) {
      if (I->isExternal())
        UndefinedSymbols.insert(I->getName());
      else if (!I->hasInternalLinkage())
        DefinedSymbols.insert(I->getName());
    }
  for (Module::giterator I = M->gbegin(), E = M->gend(); I != E; ++I)
    if (I->hasName()) {
      if (I->isExternal())
        UndefinedSymbols.insert(I->getName());
      else if (!I->hasInternalLinkage())
        DefinedSymbols.insert(I->getName());
    }
  
  // Prune out any defined symbols from the undefined symbols set...
  for (std::set<std::string>::iterator I = UndefinedSymbols.begin();
       I != UndefinedSymbols.end(); )
    if (DefinedSymbols.count(*I))
      UndefinedSymbols.erase(I++);  // This symbol really is defined!
    else
      ++I; // Keep this symbol in the undefined symbols list
}


static bool LinkLibrary(Module *M, const std::string &LibName,
                        bool search, std::string &ErrorMessage) {
  std::set<std::string> UndefinedSymbols;
  GetAllUndefinedSymbols(M, UndefinedSymbols);
  if (UndefinedSymbols.empty()) {
    if (Verbose) std::cerr << "  No symbols undefined, don't link library!\n";
    return false;  // No need to link anything in!
  }

  std::vector<Module*> Objects;
  bool isArchive;
  if (LoadLibrary(LibName, Objects, isArchive, search, ErrorMessage))
    return true;

  // Figure out which symbols are defined by all of the modules in the .a file
  std::vector<std::set<std::string> > DefinedSymbols;
  DefinedSymbols.resize(Objects.size());
  for (unsigned i = 0; i != Objects.size(); ++i)
    GetAllDefinedSymbols(Objects[i], DefinedSymbols[i]);

  bool Linked = true;
  while (Linked) {     // While we are linking in object files, loop.
    Linked = false;

    for (unsigned i = 0; i != Objects.size(); ++i) {
      // Consider whether we need to link in this module...  we only need to
      // link it in if it defines some symbol which is so far undefined.
      //
      const std::set<std::string> &DefSymbols = DefinedSymbols[i];

      bool ObjectRequired = false;
      for (std::set<std::string>::iterator I = UndefinedSymbols.begin(),
             E = UndefinedSymbols.end(); I != E; ++I)
        if (DefSymbols.count(*I)) {
          if (Verbose)
            std::cerr << "  Found object providing symbol '" << *I << "'...\n";
          ObjectRequired = true;
          break;
        }
      
      // We DO need to link this object into the program...
      if (ObjectRequired) {
        if (LinkModules(M, Objects[i], &ErrorMessage))
          return true;   // Couldn't link in the right object file...        
        
        // Since we have linked in this object, delete it from the list of
        // objects to consider in this archive file.
        std::swap(Objects[i], Objects.back());
        std::swap(DefinedSymbols[i], DefinedSymbols.back());
        Objects.pop_back();
        DefinedSymbols.pop_back();
        --i;   // Do not skip an entry
        
        // The undefined symbols set should have shrunk.
        GetAllUndefinedSymbols(M, UndefinedSymbols);
        Linked = true;  // We have linked something in!
      }
    }
  }
  
  return false;
}

static int PrintAndReturn(const char *progname, const std::string &Message,
                          const std::string &Extra = "") {
  std::cerr << progname << Extra << ": " << Message << "\n";
  return 1;
}


int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(argc, argv, " llvm linker for GCC\n");

  std::string ErrorMessage;
  std::auto_ptr<Module> Composite(LoadObject(InputFilenames[0], ErrorMessage));
  if (Composite.get() == 0)
    return PrintAndReturn(argv[0], ErrorMessage);

  // We always look first in the current directory when searching for libraries.
  LibPaths.insert(LibPaths.begin(), ".");

  // If the user specied an extra search path in their environment, respect it.
  if (char *SearchPath = getenv("LLVM_LIB_SEARCH_PATH"))
    LibPaths.push_back(SearchPath);

  for (unsigned i = 1; i < InputFilenames.size(); ++i) {
    // A user may specify an ar archive without -l, perhaps because it
    // is not installed as a library. Detect that and link the library.
    if (IsArchive(InputFilenames[i])) {
      if (Verbose) std::cerr << "Linking archive '" << InputFilenames[i]
                             << "'\n";
      if (LinkLibrary(Composite.get(), InputFilenames[i], false, ErrorMessage))
        return PrintAndReturn(argv[0], ErrorMessage,
                              ": error linking in '" + InputFilenames[i] + "'");
      continue;
    }

    std::auto_ptr<Module> M(LoadObject(InputFilenames[i], ErrorMessage));
    if (M.get() == 0)
      return PrintAndReturn(argv[0], ErrorMessage);

    if (Verbose) std::cerr << "Linking in '" << InputFilenames[i] << "'\n";

    if (LinkModules(Composite.get(), M.get(), &ErrorMessage))
      return PrintAndReturn(argv[0], ErrorMessage,
                            ": error linking in '" + InputFilenames[i] + "'");
  }

  // Remove any consecutive duplicates of the same library...
  Libraries.erase(std::unique(Libraries.begin(), Libraries.end()),
                  Libraries.end());

  // Link in all of the libraries next...
  for (unsigned i = 0; i != Libraries.size(); ++i) {
    if (Verbose) std::cerr << "Linking in library: -l" << Libraries[i] << "\n";
    if (LinkLibrary(Composite.get(), Libraries[i], true, ErrorMessage))
      return PrintAndReturn(argv[0], ErrorMessage);
  }

  // In addition to just linking the input from GCC, we also want to spiff it up
  // a little bit.  Do this now.
  //
  PassManager Passes;

  // Add an appropriate TargetData instance for this module...
  Passes.add(new TargetData("gccld", Composite.get()));

  // Linking modules together can lead to duplicated global constants, only keep
  // one copy of each constant...
  //
  Passes.add(createConstantMergePass());

  // If the -s command line option was specified, strip the symbols out of the
  // resulting program to make it smaller.  -s is a GCC option that we are
  // supporting.
  //
  if (Strip)
    Passes.add(createSymbolStrippingPass());

  // Often if the programmer does not specify proper prototypes for the
  // functions they are calling, they end up calling a vararg version of the
  // function that does not get a body filled in (the real function has typed
  // arguments).  This pass merges the two functions.
  //
  Passes.add(createFunctionResolvingPass());

  if (!NoInternalize) {
    // Now that composite has been compiled, scan through the module, looking
    // for a main function.  If main is defined, mark all other functions
    // internal.
    //
    Passes.add(createInternalizePass());
  }

  // Remove unused arguments from functions...
  //
  Passes.add(createDeadArgEliminationPass());

  // The FuncResolve pass may leave cruft around if functions were prototyped
  // differently than they were defined.  Remove this cruft.
  //
  Passes.add(createInstructionCombiningPass());

  // Delete basic blocks, which optimization passes may have killed...
  //
  Passes.add(createCFGSimplificationPass());

  // Now that we have optimized the program, discard unreachable functions...
  //
  Passes.add(createGlobalDCEPass());

  // Add the pass that writes bytecode to the output file...
  std::string RealBytecodeOutput = OutputFilename;
  if (!LinkAsLibrary) RealBytecodeOutput += ".bc";
  std::ofstream Out(RealBytecodeOutput.c_str());
  if (!Out.good())
    return PrintAndReturn(argv[0], "error opening '" + RealBytecodeOutput +
                                   "' for writing!");
  Passes.add(new WriteBytecodePass(&Out));        // Write bytecode to file...

  // Make sure that the Out file gets unlink'd from the disk if we get a SIGINT
  RemoveFileOnSignal(RealBytecodeOutput);

  // Run our queue of passes all at once now, efficiently.
  Passes.run(*Composite.get());
  Out.close();

  if (!LinkAsLibrary) {
    // Output the script to start the program...
    std::ofstream Out2(OutputFilename.c_str());
    if (!Out2.good())
      return PrintAndReturn(argv[0], "error opening '" + OutputFilename +
                                     "' for writing!");
    Out2 << "#!/bin/sh\nlli -q -abort-on-exception $0.bc $*\n";
    Out2.close();
  
    // Make the script executable...
    MakeFileExecutable (OutputFilename);

    // Make the bytecode file readable and directly executable in LLEE as well
    MakeFileExecutable (RealBytecodeOutput);
    MakeFileReadable   (RealBytecodeOutput);
  }

  return 0;
}
