add_subdirectory(context-sensitive-reachability)
target_link_libraries(libcsr PUBLIC fmt::fmt)

add_subdirectory(wpds)

# TODO: put wpds and csr here