cmake_minimum_required(VERSION 3.14)

project(twzplayground VERSION 0.1 DESCRIPTION "Twizzler Testing")

#add_executable(indexTests indexTests.c)
#install(TARGETS indexTests DESTINATION bin)

# add_executable(appendStore appendStore.c)
# install(TARGETS appendStore DESTINATION bin)

#add_executable(arrayStore arrayStore.c)
#install(TARGETS arrayStore DESTINATION bin)

add_executable(bPlusTree bPlusTree.c appendStore.c)
target_compile_options(bPlusTree PUBLIC -ggdb)
install(TARGETS bPlusTree DESTINATION bin)

add_executable(bPlusTreeArray bPlusTreeArray.c arrayStore.c)
target_compile_options(bPlusTreeArray PUBLIC -ggdb)
install(TARGETS bPlusTreeArray DESTINATION bin)

add_executable(testhello testhello.c)
install(TARGETS testhello DESTINATION bin)

add_executable(example example.c)
install(TARGETS example DESTINATION bin)

add_executable(netapi netapi.c)
install(TARGETS netapi DESTINATION bin)

target_link_libraries(netapi twznet)

add_executable(net net.cpp)
install(TARGETS net DESTINATION bin)




