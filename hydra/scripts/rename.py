#!/usr/bin/python

#######################################################################################
#
#   Copyright (c) 2006 Loyola University Chicago and Contributors. All rights reserved.
#   This file is part of The Hydra Filesystem.
#
#   The Hydra Filesystem is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
#   The Hydra Filesystem is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with the Hydra Filesystem; if not, write to the Free Software
#   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
#######################################################################################

import sys,os
sys.path.append(os.path.join(os.getcwd(),'..'))

from common import HydraFile
from common.fs_db import *
from common.fs_objects import *

if __name__ == '__main__':
      
    if len(sys.argv) < 3:
        print "Please provide two filenames as parameters."
        sys.exit(0)                
    
    src = '/'+sys.argv[1]
    dest = '/'+sys.argv[2]
    
    hf = HydraFile.HydraFile('../conf/fileclient1.xml')            
    
    try:
        hf.rename(src, dest)
        hf.open(dest, 'r')
        print hf.read()
        hf.close()
    except Exception, e:
        print e
        hf.close()

    
    # view db contents
    #d = DB('C:\support\hydrafs\db1')
    #d.setup_fs_db()
    #d.print_object_db()
    #d.close_fs_db() 
    

