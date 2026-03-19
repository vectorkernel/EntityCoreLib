// NOTE:
// Historically, InteractionState lived under EntityCoreLib/.
// It now lives in AppCoreLib so all apps share the same tool/gesture state
// (selection, hover, erase rectangle, pan/zoom, etc.).
//
// This header remains as a compatibility shim so existing projects that include
// "InteractionState.h" keep building.

#pragma once

// Prefer a configured include path (<AppCoreLib/...>) but fall back to a relative include
// for projects that only add EntityCoreLib/EntityCoreLib as an include directory.
#if defined(__has_include)
#  if __has_include(<AppCoreLib/InteractionState.h>)
#    include <AppCoreLib/InteractionState.h>
#  elif __has_include("../AppCoreLib/InteractionState.h")
#    include "../AppCoreLib/InteractionState.h"
#  else
#    error "Cannot find AppCoreLib/InteractionState.h. Add EntityCoreLib/ to your include paths."
#  endif
#else
#  include "../AppCoreLib/InteractionState.h"
#endif
