MODNAME=mod_sqs

AUTOMAKE_OPTIONS = foreign

mod_LTLIBRARIES = mod_sqs.la
mod_sqs_la_SOURCES  = mod_sqs.c
mod_sqs_la_CFLAGS   = -fPIC -g -ggdb -Dmodver=\"$(MOD_VERSION)\"
mod_sqs_la_LIBADD   = libsqshelper.a
mod_sqs_la_LDFLAGS  = -avoid-version -module -no-undefined -shared

# Define the static library
noinst_LIBRARIES = libsqshelper.a
libsqshelper_a_SOURCES = sqs_helper.cpp
libsqshelper_a_CXXFLAGS = -lstdc++ -fPIC -g -ggdb ${AWS_SDK_SQS_CFLAGS} ${AWS_SDK_SQS_LIBS}
libsqshelper_a_LIBADD =
