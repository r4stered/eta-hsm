# The strict warning set the eta_hsm tree builds under. The internal
# eta_hsm_warnings target carries these for the library's own tests and tools but
# is deliberately not exported, so the consumer verification projects apply the
# same flags to their own code through this helper.
function(eta_hsm_strict_warnings target)
    target_compile_options(
        ${target}
        PRIVATE
            "$<$<CXX_COMPILER_ID:GNU>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wnon-virtual-dtor;-Woverloaded-virtual;-Wnull-dereference;-Wdouble-promotion;-Wimplicit-fallthrough;-Wcast-align;-Wunused;-Wformat=2;-Wconversion;-Wsign-conversion;-Wold-style-cast;-Wuseless-cast;-Wsign-promo;-Werror>"
    )
endfunction()
