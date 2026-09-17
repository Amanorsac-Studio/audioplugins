#pragma once

/** Whether this build enforces the licence.

    Set by CMake from the AMANORSAC_LICENSING option: OFF for the local test
    builds used while designing and mixing, ON for anything packaged for
    customers. It defaults to ON here on purpose: a build that forgets to
    define it enforces, so a forgotten switch can never ship a plug-in that
    plays without a licence.

    When enforcement is off the plug-ins pass audio and show no activation
    page. Nothing else about the licensing code changes: the client, store and
    proof verification are all still compiled in.
*/
#ifndef AMANORSAC_LICENSING_ENABLED
 #define AMANORSAC_LICENSING_ENABLED 1
#endif
