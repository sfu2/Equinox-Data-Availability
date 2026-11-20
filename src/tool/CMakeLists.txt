add_executable(rc rc.cpp)
target_link_libraries(rc refcnt)

set(LLVM_LINK_COMPONENTS bitreader core irreader support core analysis)
llvm_map_components_to_libnames(llvm_libs ${LLVM_LINK_COMPONENTS})
target_link_libraries(rc ${llvm_libs})

add_executable(fn-extract fn_extract.cpp)
target_link_libraries(fn-extract ${llvm_libs} fmt::fmt)

add_executable(ir-parser ir_parser.cpp)
target_link_libraries(ir-parser ${llvm_libs} fmt::fmt refcnt)

add_executable(query_gen query_gen.cpp)
target_link_libraries(query_gen ${llvm_libs} fmt::fmt refcnt)

# single dyck
add_executable(ml_rc ml_rc.cpp)
target_link_libraries(ml_rc ${llvm_libs} refcnt)
add_executable(df_rc df_rc.cpp)
target_link_libraries(df_rc ${llvm_libs} refcnt)

add_executable(df_wpds df_wpds.cpp)
target_link_libraries(df_wpds ${llvm_libs} refcnt wpds)
add_executable(ml_wpds ml_wpds.cpp)
target_link_libraries(ml_wpds ${llvm_libs} refcnt wpds)

# inter dyck
add_executable(ml_inter_rc ml_inter_rc.cpp)
target_link_libraries(ml_inter_rc ${llvm_libs} refcnt)

add_executable(ml_ifds ml_ifds.cpp)
target_link_libraries(ml_ifds ${llvm_libs} refcnt)

add_executable(ml_spds ml_spds.cpp)
target_link_libraries(ml_spds ${llvm_libs} refcnt wpds)

add_executable(ml_pds_test ml_pds_test.cpp)
target_link_libraries(ml_pds_test ${llvm_libs} refcnt wpds)

add_executable(ml_pds_test1 ml_pds_test1.cpp)
target_link_libraries(ml_pds_test1 ${llvm_libs} refcnt wpds)

add_executable(df_inter_rc df_inter_rc.cpp)
target_link_libraries(df_inter_rc ${llvm_libs} refcnt)

add_executable(ml_ablation_rc ml_ablation_rc.cpp)
target_link_libraries(ml_ablation_rc ${llvm_libs} refcnt)

add_executable(df_ablation_rc df_ablation_rc.cpp)
target_link_libraries(df_ablation_rc ${llvm_libs} refcnt)

add_executable(parse_report parse_report.cpp)
target_link_libraries(parse_report ${llvm_libs} refcnt)