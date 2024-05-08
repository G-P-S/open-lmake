# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import os

	import lmake
	from lmake.rules import Rule,PyRule

	gxx = os.environ.get('CXX','g++')

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello.h'
	,	'hello.c'
	,	'world.h'
	,	'world.c'
	,	'hello_world.c'
	,	'ref'
	)

	class Compile(Rule) :
		targets = { 'OBJ' : r'{File:.*}.o' }
		deps    = { 'SRC' :  '{File}.c'    }
		cmd     = '{gxx} -fprofile-arcs -c -O0 -o {OBJ} -xc {SRC}'

	class Link(Rule) :
		targets = { 'EXE' :'hello_world' }
		deps    = {
			'H'  : 'hello.o'
		,	'W'  : 'world.o'
		,	'HW' : 'hello_world.o'
		}
		cmd = "{gxx} -fprofile-arcs -o {EXE} {' '.join((f for k,f in deps.items()))}"

	class Dut(Rule) :
		target       = 'dut'
		side_targets = { 'GCDA' : r'{File*:.*}.gcda' }
		deps         = { 'EXE'  : 'hello_world'      }
		cmd          = './{EXE}'

	class Test(Rule) :
		target = 'test'
		deps   = {
			'DUT' : 'dut'
		,	'REF' : 'ref'
		}
		cmd = 'diff ref dut'

else :

	from lmake import multi_strip
	import ut

	print(                                         'void hello() ;'                      ,file=open('hello.h','w'))
	print(                                         'void world() ;'                      ,file=open('world.h','w'))
	print('#include <stdio.h>\n#include "hello.h"\nvoid hello() { printf("hello\\n") ; }',file=open('hello.c','w'))
	print('#include <stdio.h>\n#include "world.h"\nvoid world() { printf("world\\n") ; }',file=open('world.c','w'))
	open('hello_world.c','w').write(multi_strip('''
		#include "hello.h"
		#include "world.h"
		int main() {
			hello() ;
			world() ;
			return 0 ;
		}
	'''))
	print('hello\nworld',file=open('ref','w'))

	ut.lmake( 'test' , new=6 , done=6 )
