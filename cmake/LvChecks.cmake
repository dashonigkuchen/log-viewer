# Architecture checks that run at configure time.

# Fails the configure step if the given target links any Qt library directly.
# Enforces ADR 0006 (lv_core is Qt-free).
function(lv_assert_target_has_no_qt target)
    get_target_property(_libs ${target} LINK_LIBRARIES)
    get_target_property(_ilibs ${target} INTERFACE_LINK_LIBRARIES)
    foreach(_lib IN LISTS _libs _ilibs)
        if(_lib MATCHES "Qt[0-9]*::" OR _lib MATCHES "^Qt[0-9]")
            message(FATAL_ERROR "${target} must not depend on Qt (ADR 0006), but links '${_lib}'.")
        endif()
    endforeach()
endfunction()
