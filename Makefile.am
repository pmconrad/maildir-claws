SUBDIRS = src config doc

noinst_DATA = README

README: doc/README.xml
	docbook2txt $<
	mv README.txt $@
