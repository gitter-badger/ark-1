set(kerfuffle_cli_SRCS cliplugin.cpp)

ecm_qt_declare_logging_category(kerfuffle_cli_SRCS
                                HEADER ark_debug.h
                                IDENTIFIER ARK
                                CATEGORY_NAME ark.cliexample)

add_library(kerfuffle_cli ${kerfuffle_cli_SRCS})
target_link_libraries(kerfuffle_cli KF5::KIOCore kerfuffle)

install(TARGETS kerfuffle_cli DESTINATION ${KDE_INSTALL_PLUGINDIR}/kerfuffle)

