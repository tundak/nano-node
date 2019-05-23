add_executable (btcb_rpc
	entry.cpp)

target_link_libraries (btcb_rpc
	rpc
	secure
	Boost::filesystem
	Boost::log
	Boost::log_setup
	Boost::program_options
	Boost::system
	Boost::thread
	Boost::boost)

target_compile_definitions(btcb_rpc
	PUBLIC
		-DACTIVE_NETWORK=${ACTIVE_NETWORK}
	PRIVATE
		-DBTCB_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR}
		-DBTCB_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR}
		-DBTCB_VERSION_PATCH=${CPACK_PACKAGE_VERSION_PATCH})

if ((BTCB_GUI OR RAIBLOCKS_GUI) AND NOT APPLE)
	install(TARGETS btcb_rpc
			RUNTIME DESTINATION .
	)
endif()
