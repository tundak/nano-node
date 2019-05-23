add_executable (rpc_test
	entry.cpp
	rpc.cpp)

target_link_libraries(rpc_test gtest rpc node)

target_compile_definitions(rpc_test
	PUBLIC
		-DACTIVE_NETWORK=${ACTIVE_NETWORK}
	PRIVATE
		-DBTCB_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR}
		-DBTCB_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR}
		-DBTCB_VERSION_PATCH=${CPACK_PACKAGE_VERSION_PATCH})