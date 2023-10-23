# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os
import sys
import time

import lmake

autodeps = []
if lmake.has_ptrace     : autodeps.append('ptrace'    )
if lmake.has_ld_audit   : autodeps.append('ld_audit'  )
if lmake.has_ld_preload : autodeps.append('ld_preload')

if getattr(sys,'lmake_read_makefiles',False) :

	import step

	lmake.sources = (
		'Lmakefile.py'
	,	'step.py'
	)

	lmake.config.link_support = step.link_support

	class Base(lmake.Rule) :
		stems = { 'File' : r'.*' }

	class Delay(Base) :
		target = 'dly'
		cmd    = 'sleep 0.5'

	class Src(Base) :
		target = 'hello'
		dep    = 'dly'                                                         # ensure hello construction does not start too early, so that we are sure that we have may_rerun messages, not rerun
		cmd    = f'echo hello.{step.p>=2}.{step.link_support}'

	for ad in autodeps :
		class CpyShAcc(Base) :
			name    = f'cpy-sh-acc-{ad}'
			autodep = ad
			target  = f'{{File}}.sh.acc.{ad}.{step.link_support}.cpy'
			cmd     = 'cat {File}'
		class CpyShDep(Base) :
			name    = f'cpy-sh-dep-{ad}'
			autodep = ad
			target  = f'{{File}}.sh.dep.{ad}.{step.link_support}.cpy'
			cmd     = 'ldepend {File} ; echo yes'
		class CpyPyAcc(Base) :
			name    = f'cpy-py-acc-{ad}'
			autodep = ad
			target  = f'{{File}}.py.acc.{ad}.{step.link_support}.cpy'
			def cmd() :
				sys.stdout.write(open(File).read())
		class CpyPyDep(Base) :
			name    = f'cpy-py-dep-{ad}'
			autodep = ad
			target  = f'{{File}}.py.dep.{ad}.{step.link_support}.cpy'
			def cmd() :
				lmake.depend(File,'/usr/bin/x')                                # check external dependencies are ok
				print('yes')

else :

	import ut

	n_ads = len(autodeps)

	print(f"p=0\nlink_support='none'",file=open('step.py','w'))
	ut.lmake( 'Lmakefile.py' , new=1 )                                         # prevent new Lmakefile.py in case of error as python reads it to display backtrace
	#
	for ls in ('none','file','full') :
		for p in range(3) :
			print(f'p={p!r}\nlink_support={ls!r}',file=open('step.py','w'))
			ut.lmake(
				*( f'hello.{interp}.{cmd}.{ad}.{ls}.cpy' for interp in ('sh','py') for cmd in ('acc','dep') for ad in autodeps )
			,	may_rerun=(p==0)*4*n_ads , done=(p==0 and ls=='none')+(p!=1)+(p!=1)*2*n_ads , steady=(p==0 and ls!='none')+(p!=1)*2*n_ads
			)
