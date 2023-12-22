cmake_minimum_required(VERSION 3.20)

project(
	SoundRecordDistributor
	VERSION 1.3.0
	LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(XSEPlugin)