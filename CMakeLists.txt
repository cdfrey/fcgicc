# 3.14 is the first version where cppcheck potentially behaves correctly
CMAKE_MINIMUM_REQUIRED( VERSION 3.14 )

SET( CMAKE_USER_MAKE_RULES_OVERRIDE maintainer/defaults.cmake )

PROJECT( fcgicc CXX )
SET( PROJECT_VERSION 0.2.0 )
SET( CMAKE_INSTALL_PREFIX ${PREFIX} )

FIND_PATH( FCGI_INCLUDE_DIR fastcgi.h )
IF( NOT FCGI_INCLUDE_DIR )
    FIND_PATH( FCGI_INCLUDE_DIR fastcgi.h ${PROJECT_SOURCE_DIR}/fastcgi_devkit )
ENDIF()
INCLUDE_DIRECTORIES( ${FCGI_INCLUDE_DIR} )

FIND_PROGRAM( CMAKE_CXX_CPPCHECK NAMES cppcheck )
if (CMAKE_CXX_CPPCHECK)
	list(
		APPEND CMAKE_CXX_CPPCHECK
			"--error-exitcode=1"
			"--enable=warning,style,performance,portability,information,missingInclude"
			#"--inline-suppr"
			#"--suppressions-list=${CMAKE_SOURCE_DIR}/CppCheckSuppressions.txt"
	)
endif()

ADD_SUBDIRECTORY( src )
ADD_SUBDIRECTORY( test )

INSTALL( FILES LICENSE.txt README.txt DESTINATION share/doc/${PROJECT_NAME} )

SET( DIST_FILE ${PROJECT_NAME}-${PROJECT_VERSION} )
ADD_CUSTOM_TARGET( dist ln -sf ${PROJECT_SOURCE_DIR} ${DIST_FILE} &&
    tar cjf ${DIST_FILE}.tar.bz2
        ${DIST_FILE}/LICENSE.txt
        ${DIST_FILE}/README.txt
        ${DIST_FILE}/CHANGES.txt
        ${DIST_FILE}/CMakeLists.txt
        ${DIST_FILE}/fastcgi_devkit/LICENSE.TERMS
        ${DIST_FILE}/fastcgi_devkit/fastcgi.h
        ${DIST_FILE}/src/fcgicc.cc
        ${DIST_FILE}/src/fcgicc.h
        ${DIST_FILE}/src/CMakeLists.txt
        ${DIST_FILE}/test/test1.cc
        ${DIST_FILE}/test/test2.cc
        ${DIST_FILE}/test/lighttpd.conf
        ${DIST_FILE}/test/CMakeLists.txt )

