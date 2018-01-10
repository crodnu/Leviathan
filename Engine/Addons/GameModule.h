// Leviathan Game Engine
// Copyright (c) 2012-2018 Henri Hyyryläinen
#pragma once
#include "Define.h"
// ------------------------------------ //
#include "Script/ScriptModule.h"
#include "Common/ThreadSafe.h"
#include "Events/EventableScriptObject.h"

namespace Leviathan{

//! \brief Represents a scriptable part of a program
class GameModule : public ReferenceCounted, public EventableScriptObject{
public:

    //! \warning Quite expensive constructor since it loads the definition file
    //! \todo Make load all source files, instead of loading just the first
    DLLEXPORT GameModule(const std::string &modulename, const std::string &ownername,
        const std::string &extension = "txt|levgm");
        
    DLLEXPORT ~GameModule();

    //! \brief Makes the scripts usable
    DLLEXPORT bool Init();

    //! \brief Releases the script
    //! 
    //! Use to release script before releasing any other objects
    DLLEXPORT void ReleaseScript();

    // Used to actually use the module //

    //! Executes something on the module and returns the result. Adds the module as first
    //! parameter and existed is set to true if something was executed
    DLLEXPORT std::shared_ptr<VariableBlock> ExecuteOnModule(const std::string &entrypoint,
        std::vector<std::shared_ptr<NamedVariableBlock>> &otherparams, bool &existed,
        bool fulldeclaration = false);


    //! \brief Returns a string describing this module
    DLLEXPORT std::string GetDescription(bool full);

    REFERENCE_COUNTED_PTR_TYPE(GameModule);
        
private:

    void _CallScriptListener(Event* event, GenericEvent* event2) override;
    // ------------------------------------ //

    std::string OwnerName;
    std::string LoadedFromFile;

    std::shared_ptr<ScriptModule> ScriptMain;

    // Properties loaded from the file //
    std::string Name;
    std::string Version;
    std::string SourceFile;

};

}

#ifdef LEAK_INTO_GLOBAL
using Leviathan::GameModule;
#endif

