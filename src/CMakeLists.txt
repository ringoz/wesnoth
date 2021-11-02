# store the specified sources list in the specified variable
function(GetSources source_list store_in_var)
	file(STRINGS "../source_lists/${source_list}" sources)
	set(${store_in_var} ${sources} PARENT_SCOPE)
endfunction()

if(ENABLE_MYSQL)
	execute_process(COMMAND mysql_config --cflags OUTPUT_VARIABLE MYSQL_CFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
	string(REGEX REPLACE "-I" "" MYSQL_CFLAGS "${MYSQL_CFLAGS}")
	string(REGEX REPLACE "-DNDEBUG" "" MYSQL_CFLAGS "${MYSQL_CFLAGS}")
	execute_process(COMMAND mysql_config --libs OUTPUT_VARIABLE MYSQL_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
	add_subdirectory("modules/mariadbpp/")
endif()

## some includes ##
include_directories(SYSTEM ${GETTEXT_INCLUDE_DIR})
include_directories(SYSTEM ${SDL2_INCLUDE_DIR})
include_directories(SYSTEM ${SDL2IMAGE_INCLUDE_DIR})
include_directories(SYSTEM ${SDL2MIXER_INCLUDE_DIR})
include_directories(SYSTEM ${SDL2TTF_INCLUDE_DIR})

if(ZLIB_INCLUDE_DIR)
	include_directories(SYSTEM ${ZLIB_INCLUDE_DIR} )
endif()

# needed to get include paths in the subfolders correct
include_directories( ${CMAKE_CURRENT_SOURCE_DIR} )
# needed to have the generated config.h used, too
include_directories( ${CMAKE_CURRENT_BINARY_DIR} )

set(common-external-libs ${ICU_DATA_LIBRARY} ${ICU_I18N_LIBRARY} ${ICU_UC_LIBRARY} ${SDL2_LIBRARY})

if(Intl_FOUND)
  include_directories(SYSTEM ${Intl_INCLUDE_DIR})
	set(common-external-libs ${common-external-libs} ${Intl_LIBRARIES})
endif()

if(ZLIB_FOUND)
	add_definitions(-DHAVE_ZLIB)
endif()

if(BZIP2_FOUND)
	add_definitions(-DHAVE_BZIP2)
endif()

if(OPENSSL_FOUND)
	add_definitions(-DHAVE_OPENSSL)
	set(common-external-libs ${common-external-libs} OpenSSL::Crypto OpenSSL::SSL)
	if(WIN32)
	  set(common-external-libs ${common-external-libs} crypt32.lib)
  elseif(APPLE)
	  set(common-external-libs ${common-external-libs} ${SECURITY_LIBRARY})
  endif()
endif()

set(game-external-libs
	${common-external-libs}
	${SDL2_IMAGE_LIBRARY}
	${SDL2_MIXER_LIBRARY}
	${SDL2_TTF_LIBRARY}
)

if(HISTORY_FOUND)
	set(game-external-libs ${game-external-libs} ${HISTORY_LIBRARY})
endif()

# get source lists
GetSources("lua" lua_sources)
GetSources("libwesnoth_sdl" wesnoth_sdl_sources)
GetSources("libwesnoth_widgets" wesnoth_widget-sources)
GetSources("libwesnoth" wesnoth_game_sources)
GetSources("libwesnoth_core" wesnoth_core_sources)
GetSources("wesnoth" wesnoth_sources)

# We explicitly want lua compiled as C++ version, so this line is required:
set_source_files_properties(${lua_sources} PROPERTIES LANGUAGE CXX)

# Inject a header into the Lua sources for Wesnoth-specific changes
# makedepend won't see it so we have to specifically add it as a dependency.
file(GLOB wesnoth_lua_config wesnoth_lua_config.h)
set_source_files_properties(${lua_sources} PROPERTIES OBJECT_DEPENDS ${wesnoth_lua_config})

if(MSVC)
	set_source_files_properties(${lua_sources} PROPERTIES COMPILE_FLAGS "/FI\"${wesnoth_lua_config}\" /wd4334")
else()
	set_source_files_properties(${lua_sources} PROPERTIES COMPILE_FLAGS "-include \"${wesnoth_lua_config}\" -Wno-shift-overflow")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	# silence a Clang specific warning due to extra parenthesis in if statements
	set_property(SOURCE SOURCE ${lua_sources} APPEND_STRING PROPERTY COMPILE_FLAGS " -Wno-parentheses-equality")
	# silence a Clang specific warning when compiling the lua code
	set_property(SOURCE SOURCE ${lua_sources}	APPEND_STRING PROPERTY COMPILE_FLAGS " -Wno-string-plus-int")
endif()

# Depending on the flag create a real file or an empty dummy.
#
# Since the code is executed every build run of Wesnoth we need to make sure
# that no modifications don't turn into a rebuild. Therefore a the dummy is
# created and the second target only copies the file if modified.
if(ENABLE_DISPLAY_REVISION)
	add_custom_target(wesnoth-revision_dummy
		COMMAND ${CMAKE_SOURCE_DIR}/utils/autorevision.sh -t h > ${CMAKE_CURRENT_BINARY_DIR}/revision.dummy
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	)

	add_custom_target(wesnoth-revision
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${CMAKE_CURRENT_BINARY_DIR}/revision.dummy
			${CMAKE_CURRENT_BINARY_DIR}/revision.h
	)

	add_dependencies(wesnoth-revision wesnoth-revision_dummy)
	set_source_files_properties(game_config.cpp PROPERTIES COMPILE_DEFINITIONS "LOAD_REVISION")
endif()

########### Wesnoth ###############

add_library(wesnoth-common STATIC ${wesnoth_core_sources})
target_link_libraries(wesnoth-common
	$<TARGET_NAME_IF_EXISTS:Boost::bimap>
	$<TARGET_NAME_IF_EXISTS:Boost::iostreams>
	$<TARGET_NAME_IF_EXISTS:Boost::multi_array>
	$<TARGET_NAME_IF_EXISTS:Boost::random>
)

if(ENABLE_GAME OR ENABLE_TESTS)
	add_library(wesnoth-client STATIC ${wesnoth_sources} ${lua_sources} ${wesnoth_game_sources} ${wesnoth_sdl_sources})
	target_link_libraries(wesnoth-client
		$<TARGET_NAME_IF_EXISTS:Boost::algorithm>
		$<TARGET_NAME_IF_EXISTS:Boost::align>
		$<TARGET_NAME_IF_EXISTS:Boost::any>
		$<TARGET_NAME_IF_EXISTS:Boost::bimap>
		$<TARGET_NAME_IF_EXISTS:Boost::circular_buffer>
		$<TARGET_NAME_IF_EXISTS:Boost::dynamic_bitset>
		$<TARGET_NAME_IF_EXISTS:Boost::format>
		$<TARGET_NAME_IF_EXISTS:Boost::iostreams>
		$<TARGET_NAME_IF_EXISTS:Boost::logic>
		$<TARGET_NAME_IF_EXISTS:Boost::math>
		$<TARGET_NAME_IF_EXISTS:Boost::multi_array>
		$<TARGET_NAME_IF_EXISTS:Boost::program_options>
		$<TARGET_NAME_IF_EXISTS:Boost::property_tree>
		$<TARGET_NAME_IF_EXISTS:Boost::ptr_container>
		$<TARGET_NAME_IF_EXISTS:Boost::range>
	)

	# widgets need special handling since otherwise the way they're registered causes the linker to remove them since it incorrectly thinks they're unused
	add_library(wesnoth-widgets STATIC ${wesnoth_widget-sources})
	target_link_libraries(wesnoth-widgets
		$<TARGET_NAME_IF_EXISTS:Boost::align>
		$<TARGET_NAME_IF_EXISTS:Boost::any>
		$<TARGET_NAME_IF_EXISTS:Boost::bimap>
		$<TARGET_NAME_IF_EXISTS:Boost::circular_buffer>
		$<TARGET_NAME_IF_EXISTS:Boost::dynamic_bitset>
		$<TARGET_NAME_IF_EXISTS:Boost::logic>
		$<TARGET_NAME_IF_EXISTS:Boost::multi_array>
		$<TARGET_NAME_IF_EXISTS:Boost::preprocessor>
		$<TARGET_NAME_IF_EXISTS:Boost::range>
	)
endif()

if(ENABLE_GAME)
	add_library(wesnoth-game INTERFACE)
	target_link_libraries(wesnoth-game INTERFACE
		wesnoth-common
		wesnoth-widgets
		wesnoth-client
		${game-external-libs}
	)

	if(APPLE)
		target_link_options(wesnoth-game INTERFACE LINKER:-force_load,wesnoth-widgets)
	elseif(NOT WIN32)
		target_link_options(wesnoth-game INTERFACE LINKER:--whole-archive,wesnoth-widgets,--no-whole-archive)
	else()
		target_link_options(wesnoth-game INTERFACE /WHOLEARCHIVE:$<TARGET_LINKER_FILE:wesnoth-widgets>)
	endif()

	if(WIN32)
		add_executable(wesnoth WIN32 wesnoth.cpp ../packaging/windows/wesnoth.rc)
	elseif(APPLE)
		add_executable(wesnoth MACOSX_BUNDLE wesnoth.cpp)
    set_target_properties(wesnoth PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/macos/wesnoth.plist)
	else()
		add_executable(wesnoth wesnoth.cpp)
	endif()

	target_link_libraries(wesnoth
		wesnoth-game
	)
	if(ENABLE_DISPLAY_REVISION)
		add_dependencies(wesnoth wesnoth-revision)
	endif()

	set_target_properties(wesnoth PROPERTIES OUTPUT_NAME ${BINARY_PREFIX}wesnoth${BINARY_SUFFIX})

	install(TARGETS wesnoth DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

########### Boost Unit tests ###############

if(ENABLE_TESTS)
	add_definitions(-DBOOST_TEST_DYN_LINK)

	GetSources("boost_unit_tests" boost_tests_sources)
	add_executable(boost_unit_tests ${boost_tests_sources})

	if(WIN32)
		target_link_options(boost_unit_tests PRIVATE /WX /WHOLEARCHIVE:wesnoth-widgets)
	endif()

	target_link_libraries(boost_unit_tests
		wesnoth-common
		wesnoth-widgets
		wesnoth-client
		wesnoth-common
		${game-external-libs}
		Boost::iostreams
		Boost::program_options
		Boost::regex
		Boost::system
		Boost::thread
		Boost::random
		Boost::unit_test_framework
	)
	if(ENABLE_DISPLAY_REVISION)
		add_dependencies(boost_unit_tests wesnoth-revision)
	endif()

	set_target_properties(boost_unit_tests PROPERTIES OUTPUT_NAME ${BINARY_PREFIX}boost_unit_tests${BINARY_SUFFIX})
endif()

########### Wesnothd Server ###############

if(ENABLE_SERVER)
	GetSources("wesnothd" wesnothd_sources)

	if(WIN32)
		add_executable(wesnothd ${wesnothd_sources} ../packaging/windows/wesnothd.rc)
	else()
		add_executable(wesnothd ${wesnothd_sources})
	endif()

	if(ENABLE_MYSQL)
		target_include_directories(wesnothd SYSTEM PRIVATE ${MYSQL_CFLAGS})
		target_compile_definitions(wesnothd PRIVATE HAVE_MYSQLPP)
		target_link_libraries(wesnothd mariadbclientpp)
	endif()

	target_link_libraries(wesnothd
		wesnoth-common
		${common-external-libs}
		${MYSQL_LIBS}
		$<TARGET_NAME_IF_EXISTS:Boost::asio>
		$<TARGET_NAME_IF_EXISTS:Boost::scope_exit>
	)
	if(WIN32)
		target_link_options(wesnothd PRIVATE /WX)
	endif()

	set_target_properties(wesnothd PROPERTIES OUTPUT_NAME ${BINARY_PREFIX}wesnothd${BINARY_SUFFIX})
	if(ENABLE_DISPLAY_REVISION)
		add_dependencies(wesnothd wesnoth-revision)
	endif()

	install(TARGETS wesnothd DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

########### Campaignd Server ###############

if(ENABLE_CAMPAIGN_SERVER)
	GetSources("campaignd" campaignd_sources)

	add_executable(campaignd ${campaignd_sources})

	if(ENABLE_MYSQL)
		target_include_directories(campaignd SYSTEM PRIVATE ${MYSQL_CFLAGS})
		target_compile_definitions(campaignd PRIVATE HAVE_MYSQLPP)
		target_link_libraries(campaignd mariadbclientpp)
	endif()

	target_link_libraries(
		campaignd
		wesnoth-common
		${common-external-libs}
		${MYSQL_LIBS}
		Boost::iostreams
		Boost::program_options
		Boost::regex
		Boost::system
		Boost::thread
		Boost::random
	)
	if(WIN32)
		target_link_options(campaignd PRIVATE /WX)
	endif()

	set_target_properties(campaignd PROPERTIES OUTPUT_NAME ${BINARY_PREFIX}campaignd${BINARY_SUFFIX})
	if(ENABLE_DISPLAY_REVISION)
		add_dependencies(campaignd wesnoth-revision)
	endif()

	install(TARGETS campaignd DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
