#pragma once

// NOTE:
// Selection operations are implemented in AppCoreLib so all apps share
// consistent selection/hover/command-state behavior.
//
// This header remains as a compatibility shim for older code that includes
// "EntityCoreLib/SelectionOps.h".

#if defined(__has_include)
#  if __has_include(<AppCoreLib/SelectionOps.h>)
#    include <AppCoreLib/SelectionOps.h>
#  elif __has_include("../AppCoreLib/SelectionOps.h")
#    include "../AppCoreLib/SelectionOps.h"
#  else
#    error "Cannot find AppCoreLib/SelectionOps.h. Add EntityCoreLib/ to your include paths."
#  endif
#else
#  include "../AppCoreLib/SelectionOps.h"
#endif
