import matplotlib.pyplot as plt
import numpy as np
import os
import pandas as pd
import re
import warnings

from IPython.display import Markdown, display
from os import path
from typing import Dict, List, Set

import matplotlib.ticker as mtick
import matplotlib as mpl
from scipy.stats.mstats import gmean
from matplotlib.ticker import (MultipleLocator, FormatStrFormatter,
                               AutoMinorLocator, ScalarFormatter, LogLocator)
import matplotlib.patches as patches

import seaborn as sns

c_disable_fig_save = False
c_save_loc         = '/tmp/'
c_save_prefix      = ""

def config_common(disable_fig_save=False, save_loc='/tmp', save_prefix=''):
    global c_disable_fig_save
    global c_save_loc
    global c_save_prefix
    
    c_disable_fig_save = disable_fig_save
    c_save_loc = save_loc
    c_save_prefix = save_prefix

def printmd(string):
    display(Markdown(string))

def savefig(name, usepdfbound=True):
    if c_disable_fig_save:
        return
    
    save_dir = os.path.join(c_save_loc, c_save_prefix)
    dest = os.path.join(save_dir, name + '.png')

    if os.path.exists(save_dir):
        if not os.path.isdir(c_save_loc):
            printmd('`' + c_save_loc + '` : <span style=\'color:red\'>is not a directory, FATAL</span>')
            return 
    else:
        os.mkdir(save_dir)
        
    plt.savefig(dest, bbox_inches='tight', dpi=400, transparent=False)    
    printmd('Plot saved as `' + dest + '`')
    
    plt.savefig(dest[:-4] + '.pdf', bbox_inches='tight', dpi=400, transparent=False, pad_inches=0)
    printmd('Plot saved as `' + dest + '`')
    
    if usepdfbound:
        cmd = 'pdfcrop %s.pdf %s.pdf' % (dest[:-4], dest[:-4])
        printmd('Using pdfcrop on `' + dest + '` with command `' + cmd + '`') 
        os.system(cmd)