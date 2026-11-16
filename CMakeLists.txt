# ESP-IDF 최상위 프로젝트 파일.
# idf.py가 이 파일을 읽고 main/ 과 components/ 아래 컴포넌트를 자동으로 찾는다.
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(usagi_clock)
