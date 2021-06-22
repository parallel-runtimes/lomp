# ===-- plot.py - Plot performance graphs ----------------------*- Python -*-===
#
#  Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
#  See https:llvm.org/LICENSE.txt for license information.
#  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===

import re
import sys
import os

import matplotlib

matplotlib.rcParams["errorbar.capsize"] = 2
matplotlib.use("Agg")

import matplotlib.pyplot as plt
import matplotlib.font_manager
import numpy
import math
import glob

htmlFile = None
outputDir = "."

# We scale down by this on input
scaleFactor = 1


def outputHtml(s):
    """Output a string to the html file with a trailing newline"""
    htmlFile.write(s + "\n")


def outputHtmlFileHeader(pageTitle):
    """Output the HTML boilerplate at the top of a file"""
    outputHtml(
        """
    <!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN"
            "http://www.w3.org/TR/html4/loose.dtd">
    <html lang="en">
    <head>
            <meta http-equiv="content-type" content="text/html; charset=utf-8">
            <title>"""
        + pageTitle
        + """</title>
    </head>
    <body>
    """
    )


def mean(l):
    """Compute arithmetic mean of a list"""
    return sum(l) / len(l) if len(l) != 0 else 0.0


def geomean(l):
    """Compute the geometric mean of a list"""
    return math.exp(mean([math.log(v) for v in l if v != 0]))


def standardDeviation(values, mv):
    return math.sqrt(sum([(v - mv) * (v - mv) for v in values]) / len(values))


def transpose(h):
    """Transpose a hash of hashes so that the inner keys are now outer"""
    res = {}
    for i in list(h.keys()):
        v = h[i]
        for j in list(v.keys()):
            if not res.get(j, None):
                res[j] = {}
            res[j][i] = v[j]
    return res


def convertDate(d):
    fields = re.match(r"([0-9]+)([A-Z][a-z]+)([0-9]+)", d)
    day = fields.group(1)
    month = fields.group(2)
    year = fields.group(3)
    monthOrdinal = (
        "Jan",
        "Feb",
        "Mar",
        "Apr",
        "May",
        "Jun",
        "Jul",
        "Aug",
        "Sep",
        "Oct",
        "Nov",
        "Dec",
    ).index(month)

    return int(year) * 10000 + monthOrdinal * 100 + int(day)


def extractDigits(key):
    """Split a string which may contain a number into
    a tuple of the string without the digits, and the
    integer value of the digits. We can then use that as
    a good thing to sort on, so that we get
    "a5" and "a15"
    """
    text = ""
    digits = ""
    for c in key:
        if c in "0123456789":
            digits += c
        else:
            text += c
    return (text, 0 if not digits else int(digits))


def comparePair(p1, p2):
    if p1[0] < p2[0]:
        return -1
    if p1[0] > p2[0]:
        return +1
    if p1[1] < p2[1]:
        return -1
    if p1[1] > p2[1]:
        return +1
    return 0


def compareFn(impl1, impl2):
    """ Choose a good ordering for the implementations"""
    for (v1, v2) in zip(
        [extractDigits(f.strip()) for f in impl1.split(",")],
        [extractDigits(f.strip()) for f in impl2.split(",")],
    ):
        res = comparePair(v1, v2)
        if res:
            return res
    return 0


def cmp_to_key(mycmp):
    "Convert a cmp= function into a key= function"

    class K:
        def __init__(self, obj, *args):
            self.obj = obj

        def __lt__(self, other):
            return mycmp(self.obj, other.obj) < 0

        def __gt__(self, other):
            return mycmp(self.obj, other.obj) > 0

        def __eq__(self, other):
            return mycmp(self.obj, other.obj) == 0

        def __le__(self, other):
            return mycmp(self.obj, other.obj) <= 0

        def __ge__(self, other):
            return mycmp(self.obj, other.obj) >= 0

        def __ne__(self, other):
            return mycmp(self.obj, other.obj) != 0

    return K


def pick(s, v):
    return s[v % len(s)]


def mapToIndex(specificProps, properties, idx):
    try:
        return specificProps[idx].index(properties[idx].strip())
    except:
        return 0


def moveCommonToTitle(title, results):
    # If we have properties that are common across all of our implementations,
    # we can remove them from the names of the individual experiments and into the title of the whole
    # set of readings.
    implv = list(results.keys())
    specificProps = [set() for i in range(max([len(i.split(",")) for i in implv]))]
    for impl in implv:
        for (i, p) in enumerate(impl.split(",")):
            specificProps[i] |= set((p.strip(),))

    #   print "implv: " + str(implv)
    redundantFields = [i for (i, s) in enumerate(specificProps) if len(s) == 1]
    #   print "redundantFields: " + str(redundantFields)

    if len(redundantFields) == 0:
        return title

    # Yay, we have something we can handle
    # First extract the relevant fields from any of the implementations
    fields = [
        p.strip()
        for (i, p) in enumerate(implv[0].split(","))
        if i in redundantFields and p.strip() != ""
    ]
    if fields:
        title = title + ": " + (",".join(fields))
    for impl in implv:
        fields = [p.strip() for p in impl.split(",")]
        newName = ",".join(
            [fields[i] for i in range(len(fields)) if i not in redundantFields]
        )
        results[newName] = results.pop(impl)

    return title


def filterCorrelations(specificProps, implv):
    pass


def computeStyles(implv, monotone=False):
    """Compute the line colour and point style for each implementation once
    so that all plots are consistent.
    """
    # The lengths of the styles and colours lists should be co-prime,
    # so that you don't get replication of the same style and colour before
    # you have seen the product of the two lengths.
    styles = ("+", "*", "o", "^", "D", "v", "x", "p", "s", "h")
    if monotone:
        colours = ("black",)
    else:
        colours = (
            "blue",
            "red",
            "green",
            "black",
            "orange",
            "pink",
            "turquoise",
            "indigo",
            "violet",
            "cyan",
            "sienna",
            "chartreuse",
            "darkviolet",
            "orchid",
            "gold",
            "hotpink",
        )
    # See https://matplotlib.org/3.1.1/gallery/lines_bars_and_markers/linestyles.html?highlight=linestyle
    # for details of line styles and the meaning of the numeric lists!
    linestyles = (
        "solid",
        "dotted",
        "dashed",
        "dashdot",
        (0, (1, 10)),
        (0, (3, 5, 1, 5)),
    )

    allStyles = {"styles": styles, "colours": colours, "linestyles": linestyles}

    # We try to be more specific, to make things easier to understand
    specificProps = [set() for i in range(max([len(i.split(",")) for i in implv]))]
    for impl in implv:
        for (i, p) in enumerate(impl.split(",")):
            specificProps[i] |= set((p.strip(),))

    # There may be redundancy here, though. (E.g. a set of KNC readings all on Jan 1 and KNL readings all on Jan 2
    # Try to filter that out (currently does nothing...)
    filterCorrelations(specificProps, implv)
    if False:
        print("SpecificProps:")
        for s in specificProps:
            print(str(s) + " length: " + str(len(s)))
    res = {}
    # If there are properties which are invariant, we don't need to reflect them
    # in the encodings, we have three dimensions we can use to map in a coherent manner
    requiredDimensions = sum([len(p) > 1 for p in specificProps])
    # print ("Required dimensions = ", requiredDimensions)

    if requiredDimensions > 0 and requiredDimensions <= 3:
        # Try to find a good mapping so that a specific property has the same visual representation
        encodingToProperty = dict([(k, -1) for k in list(allStyles.keys())])
        nextProp = 0
        used = dict([(k, False) for k in list(allStyles.keys())])

        # If all of the properties have the same number of values, then just allocate based on the order in
        # which they appeared, mapping 1st => colour, second->style, third -> linestyle
        propLens = [len(p) for p in specificProps if len(p) != 1]
        # print("specificProps: ", specificProps, " propLens: ", propLens)
        l0 = propLens[0]
        if l0 < min([len(s) for s in list(allStyles.values())]) and all(
            [n == l0 for n in propLens]
        ):
            styleNames = sorted(
                list(allStyles.keys())
            )  # Sorted here works because colour comes first!
            pos = 0
            for (i, p) in enumerate(specificProps):
                if len(p) == 1:
                    continue
                prop = styleNames[pos]
                used[prop] = True
                encodingToProperty[prop] = i
                pos += 1
        else:
            for (i, p) in enumerate(specificProps):
                if len(p) == 1:
                    continue

                deltaLen = dict(
                    [
                        (
                            k,
                            len(allStyles[k]) - len(p)
                            if (len(allStyles[k]) >= len(p) and not used[k])
                            else 10000,
                        )
                        for k in list(allStyles.keys())
                    ]
                )
                # print ("Looking for encoding for " + str(p) + " ["+str(i)+"]")
                # Find the least wasteful property
                minDelta = min(deltaLen.values())
                if minDelta == 10000:
                    continue
                for k in list(allStyles.keys()):
                    if deltaLen[k] == minDelta:
                        bestProp = k
                        break
                used[bestProp] = True
                encodingToProperty[bestProp] = i
                # print ("Assigned encoding " + bestProp + " to " + str(i))

        # print ("encodingToProperty = " + str(encodingToProperty))
        # print ("used = " + str(used))

        # Force colour onto the first interesting property if we didn't already use it.
        if not used["colours"]:
            for (i, p) in enumerate(specificProps):
                if len(p) == 1:
                    continue
                used["colours"] = True
                for k in list(encodingToProperty.keys()):
                    if encodingToProperty[k] == i:
                        used[k] = 0
                        break
                encodingToProperty["colours"] = i
                # print "Colour not used, so using it for " + str(i)
                break

        # Check that we managed to find a sensible mapping, otherwise we use the incoherent
        # mapping, which ensures that each entry is unique.
        if sum(used.values()) == requiredDimensions:
            # print ("encodingToProperty = ", encodingToProperty)
            lv = [sorted(list(p), key=cmp_to_key(compareFn)) for p in specificProps]
            for impl in implv:
                properties = impl.split(",")
                # print ("properties = " + str(properties))
                res[impl] = (
                    pick(
                        styles, mapToIndex(lv, properties, encodingToProperty["styles"])
                    ),
                    pick(
                        colours,
                        mapToIndex(lv, properties, encodingToProperty["colours"]),
                    ),
                    pick(
                        linestyles,
                        mapToIndex(lv, properties, encodingToProperty["linestyles"]),
                    ),
                )
            return res
    # Failed to find a good mapping so use the safe default
    for i, impl in enumerate(sorted(implv, key=cmp_to_key(compareFn))):
        res[impl] = (pick(styles, i), pick(colours, i), "-")

    return res


def setupXAxis(plot, minVal, maxVal, label, logarithmic):
    """Set up the X axis, including scaling, labels and max/min values"""
    plot.set_xlabel(label)

    if logarithmic:
        plot.set_xscale("log")
        plot.set_xlim(minVal, maxVal)
    #     plot.set_xscale('log', basex=2)
    #     tickLabels = [1]
    #     labelValue = minVal
    #     while labelValue <= maxVal:
    #         tickLabels.append (labelValue)
    #         labelValue = labelValue*2
    # # Expand the axis a little above and below the data
    #     inflationFactor = 0.95
    #     plot.set_xlim(minVal*inflationFactor, maxVal/inflationFactor)
    #     # Need a blank label on the front for the added axis point on the left. No need for an extra
    #     # annotation on the right.
    #     plot.set_xticklabels([' '] + tickLabels)
    else:
        plot.set_xlim((0 if minVal == 1 else minVal), maxVal)


def addLegend(ax, lines, impls, legendPos):
    """Add the legend to the plot, shrinking the plot slightly to make
    room, since we add the legend outside the plot to the right, or leaving the plot
    full sized and allowing matplotlib to choose a good placement"""

    # If there's only one piece of data being plotted, there's no need for a legend
    # since all the parameters will be in the title.
    # Compute the length (in characters) of the longest implementation.
    legendLen = max(list(map(len, impls)))
    if legendLen == 0:
        return
    legendItems = len(impls)
    fontSize = 10 if legendLen < 20 and legendItems <= 4 else 8
    prop = matplotlib.font_manager.FontProperties(size=fontSize)
    if legendPos in (
        "best",
        "upper right",
        "upper left",
        "lower right",
        "lower left",
        "right",
        "center right",
        "center left",
        "lower center",
        "upper center",
        "center",
    ):
        ax.legend(lines, impls, prop=prop, loc=legendPos)
    elif legendPos == "below":
        # Place the legend below the x-axis
        axisShrink = 0.15 if legendItems < 7 else 0.2
        box = ax.get_position()
        newHeight = box.height * (1 - axisShrink)
        ax.set_position([box.x0, box.y0 + box.height - newHeight, box.width, newHeight])
        ax.legend(
            lines,
            impls,
            prop=prop,
            bbox_to_anchor=(0, -0.1),
            borderaxespad=0.0,
            loc="upper left",
        )
    else:
        # Place the legend on the right
        # Shink current axis by 15% to make room for the legend on the right.
        # If we were smarter we'd work out how much we need to shrink based on the
        # size of the legend box and so on, but this is OK for now.
        # See how much we think we need to shrink to fit in the legend
        axisShrink = 0.15 if legendLen < 20 else 0.2
        box = ax.get_position()
        ax.set_position([box.x0, box.y0, box.width * (1 - axisShrink), box.height])
        ax.legend(
            lines,
            impls,
            prop=prop,
            bbox_to_anchor=(1.02, 1),
            borderaxespad=0.0,
            loc="upper left",
        )


def splitXY(pos):
    """Split out the x and y values from "[x-y]"
    """
    (f,b) = pos.split('-')
    return (int(f[1:]),int(b[:-1]))

def preparePlot(bmName):
    # For "European" size paper a sqrt(2) aspect ratio is good, since that's
    # the page ratio.
    aspectRatio = math.sqrt(2)
    widthInInches = 6
    fig = plt.figure(figsize=(widthInInches * aspectRatio, widthInInches), dpi=1200)
    plt.title(bmName)
    return fig

def getEncoding(yAxisName,timeUnit):
    if timeUnit:
        encoding = (yAxisName, timeUnit)
    else:
        encoding = {
                "Time(ticks)": ("Time", " T",),
                "Time": ("Time", "s"),
                "Throughput": ("Throughput", "Op/s"),
                "Machine Throughput": ("Machine Throughput", "Op/s"),
                "Percent": ("Percent", "%"),
                "Percentage": ("Percent", "%"),
                "Efficiency (%)": ("Efficiency", "%"),
                "Efficiency": ("Efficiency", "%"),
                "Speedup": ("Speedup", "x"),
        }.get(yAxisName, None)
    return encoding
        
def finalisePlot(ax,bmName,yAxisName,fig,fileSuffix,timeUnit):
    if "Normalized" in bmName:
        from matplotlib.ticker import StrMethodFormatter

        decimals = 2 if yTMdelta < 0.1 else (1 if yTMdelta < 1.0 else 0)
        ax.yaxis.set_major_formatter(StrMethodFormatter("{x:." + str(decimals) + "f}x"))
        encoding = None
    else:
        encoding = getEncoding(yAxisName, timeUnit)

    if encoding:
        # print ("Encoding:", encoding)
        from matplotlib.ticker import EngFormatter

        ax.yaxis.set_major_formatter(EngFormatter(unit=encoding[1], sep=""))
        yAxisName = encoding[0]

    # print ("yAxisName:", yAxisName)
    ax.set_ylabel(yAxisName)

    fname = cleanFilename(bmName) + fileSuffix
    # Explicitly save the file to the output directory
    print("Saving " + os.path.join(outputDir, fname + ".png"))
    fig.savefig(os.path.join(outputDir, fname + ".png"), transparent=True)
    # Can we save eps or pdf? That's what we need for publication, and ... it just works!
    # print("Saving " + os.path.join(outputDir, fname + ".pdf"))
    # fig.savefig(os.path.join(outputDir, fname + ".pdf"), transparent=True)
    # If we want eps re-enable the lines below
    #    print("Saving " + os.path.join(outputDir, fname + ".eps"))
    #    fig.savefig(os.path.join(outputDir, fname + ".eps"), transparent=True)
    plt.close()
    fname = fname + ".png"
    # But reference it assuming that the HTML and plot are in the same directory
    outputHtml("<a href=" + fname + ">")

    width = 1000
    aspectRatio = math.sqrt(2)
    height = int(width / aspectRatio)
    outputHtml(
        "<img src="
        + fname
        + " alt="
        + fname
        + " width="
        + str(width)
        + " height="
        + str(height)
        + "/img>"
    )
    outputHtml("</a>")


def extractGrid(npl,sizeValues):
    """Extract the grid positions from 2D data where the key is [from-to]"""
    xposns = []
    yposns = []
    data   = []
    curY   = -1
    rowData= None
    # print ("npl: ",npl)
    # print ("sizeValues: ",sizeValues)
    
    for (pos,value) in zip(npl,sizeValues['']):
        (ypos,xpos) = splitXY(pos)
        yposns.append(ypos)
        xposns.append(xpos)
        if ypos != curY:
            curY = ypos
            if rowData:
                data.append(rowData)
            rowData = []
        rowData.append(value)
        
    if len(rowData):
        data.append(rowData)
    return (xposns,yposns,data)
    
def generateHeatmap(bmName,yAxisName,npl,sizeValues,fileSuffix,timeUnit):
    # We're hoping this is the tag for a heatmap
    # print("data: ",data)
    fig = preparePlot(bmName)
    ax = fig.axes[0]
    impls = sorted(list(sizeValues.keys()), key=cmp_to_key(compareFn))
    if len(impls) != 1:
        print ("***Cannot have more than one implementation in a heatmap")
        return

    (xposns,yposns,data) = extractGrid(npl,sizeValues)
     
    # Do most of the work!
    im = ax.imshow(data,cmap=plt.get_cmap("plasma"))
    ax.set_xlabel("To")
    # We want to show all ticks...
    ax.set_xticks(xposns)
    ax.set_yticks(yposns)
    # ... and label them with the respective list entries
    ax.set_xticklabels([str(xp) for xp in xposns])
    ax.set_yticklabels([str(yp) for yp in yposns])
    ax.set_ylabel("From")

    # Add a colorbar
    cBar = plt.colorbar(im)
    finalisePlot(cBar.ax,bmName+"_map", "",fig,fileSuffix,"s")

def generatePlot(
    bmName,
    yAxisName,
    npl,
    sizeValues,
    deviations=None,
    yMax=None,
    yMin=None,
    yLines=(),
    fileSuffix="",
    xMin=None,
    xLabel="",
    logarithmic=False,
    legendPos="best",
    sortKeyFn=lambda x: x,
    timeUnit=None,
):
    """Generate a single plot, which has various options.
    The maximum y axis value can be set
    Grid lines can be plotted across the graph
    Error bars can be plotted
    If the axis values are strings, then we assume that they represent a pair of the form
    "[i-j]" where i and j are integers as a co-ordinate in x,y space, which we decode
    and plot a heat map.
    """
    if isinstance(npl[0], str):
        generateHeatmap(bmName,yAxisName,npl,sizeValues,fileSuffix,timeUnit)
        return
    
    print("Plot: '" + bmName + "'")
    fig = preparePlot(bmName)
    ax = fig.axes[0]
    impls = sorted(list(sizeValues.keys()), key=cmp_to_key(compareFn))
    # print("xmMin = ",xMin)
    setupXAxis(ax, npl[0] if xMin == None else xMin, npl[-1], xLabel, logarithmic)

    if False:
        print ("npl: ", npl)
        print ("sizeValues: ", sizeValues)
        print ("impls: ",impls)
    
    lines = []

    # Choose a marker size based on the number of points we're plotting in all
    numPoints = sum([len(sizeValues[impl]) for impl in impls])
    markerSize = 5 if numPoints < 1000 else 2.5
    # print ("numPoints: ",numPoints, " markerSize: ",markerSize)
    for impl in impls:
        dataValues = sizeValues[impl]
        nplToUse = npl
        if len(dataValues) != len(npl):
            # print "impl : " +impl
            # print "npl : " + str(npl) + " dataValues: " + str(dataValues)
            nplToUse = npl[: len(dataValues)]
        # print ("impl: ", str(impl), " " + str(dataValues))
        (line,) = ax.plot(nplToUse, dataValues)
        lines.append(line)
        plt.setp(
            line,
            marker=implementationStyles[impl][0],
            markersize=markerSize,
            color=implementationStyles[impl][1],
            linestyle=implementationStyles[impl][2]
        )

        if deviations:
            # ax.errorbar is inconsistent with ax.plot, and doesn't ignore None entries
            # so we have to add the bars one at a time ignoring the Nones ourself
            for x, v, e in zip(nplToUse, dataValues, deviations[impl]):
                if v is None:
                    continue
                print ("x:",x,"v:",v)
                ax.errorbar(x, v, yerr=e, color=implementationStyles[impl][1])
    addLegend(ax, lines, impls, legendPos)
    # Round up the yMax value so that it is at the granularity of the y axis tick marks
    yTicks = ax.get_yticks()
    yTMdelta = yTicks[1] - yTicks[0]
    # print(bmName," yMax=",yMax,"yTMdelta = ",yTMdelta)
    if yMax == None:
        yMax = yTicks[-1]
    else:
        yMax = yTMdelta * math.ceil(yMax / yTMdelta)
    print("Computed yMax: ", yMax)

    ax.set_ylim(yMin, yMax)
    # And similarly for xMin
    xTicks = ax.get_xticks()
    xTMdelta = xTicks[1] - xTicks[0]
    xTickMin = int(10 ** xTicks[0]) if logarithmic else xTicks[0]
    # print ("Incoming xMin:",xMin, " xTicks[0]: ", xTickMin)
    if xMin == None or xMin == xTickMin:
        xMin = xTicks[0]
    else:
        xMin = xTMdelta * math.floor(xMin / xTMdelta)
    ax.set_xlim(10 ** xMin if logarithmic else xMin)
    # print ("xMin computed as ",xMin)

    if yLines:
        for l in yLines:
            ax.axhline(y=l, color="gray")
    else:
        ax.grid(True)

    finalisePlot(ax, bmName,yAxisName,fig,fileSuffix,timeUnit)
    
def generateBarChart(bmName, yAxisName, bins, fileSuffix="", xLabel=""):
    """Generate a single bar chart
    """
    fig = plt.figure()
    plt.title(bmName)
    ax = fig.add_subplot(111)
    setupXAxis(ax, 1, len(bins), xLabel, False)
    # print "npl: " + str(npl) + " sizeValues " + str(sizeValues)
    ax.bar(numpy.arange(len(bins)), bins, 1, color="blue")
    ax.set_ylim(bottom=0)
    #     for l in yLines:
    #         ax.axhline(y=l,color='gray')
    ax.set_ylabel(yAxisName)
    fname = re.sub(" ", "_", bmName) + "_bar" + fileSuffix + ".png"
    # Explicitly save the file to the output directory
    fig.savefig(os.path.join(outputDir, fname), transparent=True)
    # But reference it assuming that the HTML and plot are in the same firectory
    outputHtml("<a href=" + fname + ">")
    outputHtml("<img src=" + fname + " alt=" + fname + " width=800 height=750/>")
    outputHtml("</a>")


def outputHtmlTableHeader(headers):
    outputHtml("<tr>")
    for i in headers:
        outputHtml("<td align=center>" + i + "</td>")
    outputHtml("</tr>")


def extractColumnKeys(array):
    """Extract a sorted list of all the second level keys"""
    columnKeys = set()
    for i in list(array.values()):
        columnKeys |= set(i.keys())
    return sorted(list(columnKeys), key=cmp_to_key(compareFn))


def outputHtmlTitle(text):
    outputHtml("<h1>" + text + "</h1>")


def outputHtmlTable(
    leftmostTitle,
    array,
    formatFn=lambda x: str(x),
    valueFn=lambda x: x,
    best="Lowest",
    okPercent=5,
):
    """Print an HTML table from a two deep hash.
    The top level hash keys form the row titles
    The second level keys the column titles
    """
    # Work out what all the column titles should be, we can't assume that each
    # row has all of the same data entries

    # Sort the numeric entries, then add the non-numeric keys (averages).
    rowKeys = sorted([v for v in array.keys() if not isinstance(v, str)])
    rowKeys += [v for v in array.keys() if isinstance(v, str)]
    columnKeys = extractColumnKeys(array)
    # print ("columnKeys = ", columnKeys)
    if best == "Lowest":
        extremeOp = min
        compareOp = lambda x, y: x < y
        deltaOp = lambda x, y: x + y
    else:
        extremeOp = max
        compareOp = lambda x, y: x > y
        deltaOp = lambda x, y: x - y

    outputHtml("<table border=1>")
    outputHtmlTableHeader([leftmostTitle] + [str(k) for k in columnKeys])
    for k in rowKeys:
        outputHtml("<tr>")
        outputHtml("<td align=right>" + str(k) + "</td>")
        allVals = [array[k].get(ck, None) for ck in columnKeys]
        if extremeOp:
            values = [valueFn(v) for v in allVals if v != None and valueFn(v) != None]
            extremeVal = extremeOp(values)
            goodMax = extremeVal * deltaOp(1.0, (okPercent / 100.0))
            for v in allVals:
                if v == None:
                    outputHtml("<td align=right>  </td>")
                    continue
                text = formatFn(v)
                value = valueFn(v)
                if value == extremeVal:
                    text = '<font color="FF4500">' + text + "</font color></td>"
                if compareOp(value, goodMax):
                    outputHtml("<td align=right bgcolor=#99EB99>" + text + "</td>")
                else:
                    outputHtml("<td align=right>" + text + "</td>")
        else:
            for v in allVals:
                outputHtml(
                    "<td align=right>" + (formatFn(v) if v != None else " ") + "</td>"
                )
        outputHtml("</tr>")
    outputHtml("</table><br>")

    # Return the name of the column with the last extreme value.
    # In many cases that is the overall mean...
    return columnKeys[values.index(extremeVal)] if extremeOp != None else None


def mediaWikiTable(leftmostTitle, array, formatFn=lambda x: str(x)):
    """Output a media wiki formatted table"""
    columnKeys = extractColumnKeys(array)
    print("{|")
    for t in [leftmostTitle] + [str(k) for k in columnKeys]:
        print("!" + " !! ".join(titles))
    for k in sorted(array.keys, key=cmp_to_key(compareFn)):
        print("|-")
        print("| " + str(k))
        v = array[k]
        for ck in columnKeys:
            value = v.get(k, None)
            print("| " + (formatFn(value) if value else ""))
    print("|}")


def linearFit(title, threadCounts, bestTimes, independentVar, unit):
    """Print info on a linear fit"""
    outputHtml("<h1>" + title + "</h1>")
    results = {}
    impls = sorted(list(bestTimes.keys()), key=cmp_to_key(compareFn))
    outputHtml('<table border="1">')
    if independentVar[-1] == "s":
        independentVar = independentVar[:-1]
    outputHtmlTableHeader(
        ["Implementation", "  Tzero  ", "Per " + independentVar + " " + unit]
    )
    for impl in impls:
        outputHtml("<tr>")
        outputHtml("<td align=left>" + impl + "</td>")
        coeffs = numpy.polyfit(threadCounts, bestTimes[impl], 1)
        results[impl] = coeffs
        for i in (1, 0):
            outputHtml("<td align=right>" + engFormat(coeffs[i]) + "</td>")
        outputHtml("</tr>")
    outputHtml("</table><br>")
    return results


def plotFit(title, threadCounts, bestTimes, coeffs, independentVar, unit):
    """Plot the data and best fit for implementations that contain the given key"""
    values = {}
    for impl in sorted(list(bestTimes.keys()), key=cmp_to_key(compareFn)):
        values[impl] = bestTimes[impl]
        (tThread, tZero) = coeffs[impl]
        values[impl + "(best fit)"] = [
            tZero + thread * tThread for thread in threadCounts
        ]
    generatePlot(
        title, unit, threadCounts, values, xLabel=independentVar, logarithmic=False
    )


# Code to read the SI formatted data.
def extractSI(s):
    """Convert a measurement with a range suffix into a suitably scaled value"""

    # If this is representing a range, just return it as is.
    if "[" in s:
        return (s,None)

    types = {"T": "Time", "O": "Rate", "A": "Rate", "s": "Seconds", "%": "Percentage"}
    du = s.split()
    # Preserve integers as such, so that columns like "Threads" generate an X axis "1 2 3",
    # rather than "1.0 2.0 3.0"
    num = float(du[0]) if "." in du[0] else int(du[0])
    units = du[1] if len(du) == 2 else " "
    if s[-1] == " ":
        units = units + " "

    # http://physics.nist.gov/cuu/Units/prefixes.html
    factor = {
        "Y": 1e24,
        "Z": 1e21,
        "E": 1e18,
        "P": 1e15,
        "T": 1e12,
        "G": 1e9,
        "M": 1e6,
        "k": 1e3,
        " ": 1,
        "m": -1e3,  # Yes, I do mean that, see below for the explanation.
        "u": -1e6,
        "n": -1e9,
        "p": -1e12,
        "f": -1e15,
        "a": -1e18,
        "z": -1e21,
        "y": -1e24,
    }[units[0] if len(units) == 2 else " "]
    # print ("units = '" + units + "'" + " factor=" + str(factor))

    # Minor trickery here is an attempt to preserve accuracy by using a single divide,
    # rather than  multiplying by 1/x, which introduces two roundings since 1/10 is not representable
    # perfectly in IEEE floating point.
    # (Not that this really matters, other than for cleanliness, since we're likely reading numbers with
    # at most five decimal digits of precision).
    return (
        num * factor if factor > 0 else num / -factor,
        types.get(units[-1], "Count"),
    )


def readData(f):
    """Load data into a Pandas' data frame, we're not using this yet..."""
    line = f.readline()
    fieldnames = [x.strip() for x in line.split(",")]
    line = f.readline().strip()
    data = []
    while line != "":
        if line[0] != "#":
            fields = line.split(",")
            data.append((fields[0], [extractSI(v)[0] for v in fields[1:]]))
        line = f.readline().strip()
    # Man, working out this next incantation out was non-trivial!
    # They really want you to be snarfing data in csv or some other format they understand!
    res = pd.DataFrame.from_items(data, columns=fieldnames[1:], orient="index")
    return res


def extractTime(s):
    """Extract a time from a string of the form "%dm %4.2fs"
    which is what "time" generates.
    """
    msRe = r"([0-9]+)m +([0-9]+\.[0-9]+)s"
    matched = re.match(msRe, s)
    if matched:
        return 60 * int(matched.group(1)) + float(matched.group(2))
    # Maybe we don't have any minutes
    sRe = r"([0-9]+\.[0-9]+)s"
    matched = re.match(sRe, s)
    if matched:
        return float(matched.group(1))


def engFormat(f):
    """Format a number in engineering format, where the exponent is a multiple of 3"""
    if f == 0.0:
        value = 0.0
        exponent = 0
    else:
        exponent = math.log10(-f if f < 0 else f)
        if exponent < 0:
            exponent = -int(math.ceil(-exponent))
        else:
            exponent = int(math.floor(exponent))
        for i in range(3):
            if (exponent % 3) == 0:
                break
            exponent = exponent - 1
        value = f * 10 ** -exponent
    # Choose a format to maintain the number of useful digits we print.
    if abs(value) < 10:
        fmt = "%6.3f%s"
    elif abs(value) < 100:
        fmt = "%6.2f%s"
    else:
        fmt = "%6.1f%s"

    return fmt % (value, ("" if exponent == 0 else "e%d" % exponent))


class measurement:
    formatFunctions = {}
    units = {}

    def __init__(self, fieldnames, values, units=None):
        if units == None:
            # print ("No units for ", fieldnames)
            units = [measurement.units.get(name, None) for name in fieldnames]
        if len(values) != len(fieldnames):
            raise Exception(
                "Wrong number of fields: have "
                + str(len(values))
                + " values for "
                + str(fieldnames)
            )
        for (field, unit, value) in zip(fieldnames, units, values):
            self.__dict__[field] = value
            self.rememberUnit(field, unit)

    def __repr__(self):
        return str(self.__dict__)

    def rememberUnit(self, name, unit):
        if unit == None:
            measurement.formatFunctions[name] = lambda x: ("%d" % x)
            measurement.units[name] = "Count"
        else:
            measurement.formatFunctions[name] = engFormat
            measurement.units[name] = unit
        # print ("Saving unit ", name, "=", unit)

    def addField(self, name, value, unit=None):
        self.__dict__[name] = value
        self.rememberUnit(name, unit)

    def getValue(self, name):
        return self.__dict__.get(name, None)

    def formatValue(self, name):
        value = self.getValue(name)
        return "None" if value == None else measurement.formatFunctions[name](value)


def findAllStats():
    return set([x for x in list(measurement.units.keys()) if not "SD" in x])


def convertMinMaxIntoError(m, name, minName, maxName):
    """If a measurement has Min and Max, we can convert them into a notional error bar
    by replacing the name_SD field with a [minName-value, maxName-value] pair"""
    minVal = m.__dict__.get(minName, None)
    maxVal = m.__dict__.get(maxName, None)
    if maxVal == None or minVal == None:
        return None
    value = m.__dict__[name]
    return [[value - minVal], [maxVal - value]]


def summarizeResults(results, what, discard=(), minName=None, maxName=None):
    """Extract a list of thread counts,
    The times per benchmark/thread, and standard deviations per benchmark/thread"""
    #    threadCounts = sorted (transpose(results).keys())
    threadCounts = sorted(
        [t for t in list(transpose(results).keys()) if t not in discard]
    )
    sds = {}
    meanValues = {}
    what_SD = "SD" if what == "Mean" else what + "_SD"

    for k in list(results.keys()):
        res = results[k]
        meanValues[k] = [
            None if res.get(tc, None) == None else res[tc].__dict__.get(what, None)
            for tc in threadCounts
        ]
        if minName != None and maxName != None:
            sds[k] = [
                None
                if res.get(tc, None) == None
                else convertMinMaxIntoError(res[tc], what, minName, maxName)
                for tc in threadCounts
            ]
        else:
            sds[k] = [
                None
                if res.get(tc, None) == None
                else res[tc].__dict__.get(what_SD, None)
                for tc in threadCounts
            ]
    return (threadCounts, meanValues, sds)


def selectedT1(results, what, doMin=True):
    """Extract a minimum or maximum value for the given key on the fewest number of threads we measured on (anywhere)"""
    selectedCount = min(
        [k for k in transpose(results).keys() if not isinstance(k, str)]
    )
    selectedVal = 1e9 if doMin else -1e9

    # print "selectedCount " + str(selectedCount)

    comparison = min if doMin else max
    for k in list(results.keys()):
        res = results[k]
        thisVal = (
            None
            if res.get(selectedCount, None) == None
            else res[selectedCount].__dict__.get(what, None)
        )
        if thisVal != None:
            selectedVal = comparison(selectedVal, thisVal)
    if selectedCount != 1:
        print("Using time for " + str(selectedCount) + " threads as scale basis")
    if doMin:
        return (selectedVal * selectedCount, selectedCount)
    else:
        return (selectedVal / selectedCount, selectedCount)


def transformResults(threadCounts, values, function):
    res = {}
    for bm in list(values.keys()):
        res[bm] = []
        for (nThreads, v) in zip(threadCounts, values[bm]):
            res[bm].append(None if v == None else function(v, nThreads))
    return res


def computeStats(results, independentVariable):
    """Compute mean and standard deviation results for duplicate entries
       in the list of measurements.
    """
    common = {}

    # Collect lists of the values
    for v in results:
        test = v.__dict__[independentVariable]
        try:
            common[test].append(v)
        except:
            common[test] = [v]

    result = []
    # Sanity check for number of data items being summarized
    # print ("Computing stats on " + str(len(common.values()[0])))
    for measurements in list(common.values()):
        resultValues = {}
        resultValues[independentVariable] = measurements[0].__dict__[
            independentVariable
        ]
        # print ("Measurement[\""+independentVariable+"\"] : " + str(resultValues[independentVariable]))
        fieldnames = list(measurements[0].__dict__.keys())
        fieldnames.remove(independentVariable)
        for stat in fieldnames:
            values = [m.__dict__[stat] for m in measurements]
            resultValues[stat] = mean(values)
            resultValues[stat + "_SD"] = standardDeviation(values, resultValues[stat])
        result.append(
            measurement(list(resultValues.keys()), list(resultValues.values()))
        )
        # print ("Result: " + str(result))
    return result


def needStat(title, stat):
    return stat.lower() not in title.lower()


# Match a statistic output by "perf -e "
perfOutputLineRe = re.compile("^\s*([0-9,]+)\s([a-zA-Z0-9\-]+)\s*$")


def computeSelection(fields, desiredFields):
    # Ensure that the order of the fields selected is the same as the order in the desiredFields list.
    res = []
    for df in desiredFields:
        for (idx, f) in enumerate(fields):
            if f == df:
                res.append(idx)
                break
    return res


def addOverallMeans(results, fieldNames, fields):
    """Add an overall mean for the given field"""
    # Work out what the values we already have look like
    meanValues = ["Overall Mean"]
    geoMeanValues = ["Overall Geometric Mean"]
    for name in fieldNames[1:]:
        if name in fields:
            values = [r.__dict__[name] for r in results]
            geoMeanValues.append(geomean(values))
            meanValues.append(mean(values))
        else:
            geoMeanValues.append(0)
            meanValues.append(0)

    results.append(measurement(fieldNames, meanValues))
    results.append(measurement(fieldNames, geoMeanValues))
    return results


def computeGoodMax(totalTimes, noerrs):
    """Find a good value for the maximum on the Y axis"""
    # Could allow a small amount of space above the top, but it's annnoying for percentages!
    # return None
    factor = 1.00
    maxReading = factor * max(
        [max([v for v in l if v != None]) for l in list(totalTimes.values())]
    )
    if maxReading == 0:
        maxReading = 0.1
    decade = math.floor(math.log10(maxReading))
    scaledValue = maxReading * 10 ** (-decade)
    # print ("maxReading: ",maxReading,"decade: ",decade," scaledValue: ",scaledValue)
    for v in (
        1.0,
        1.1,
        1.2,
        1.25,
        1.3,
        1.4,
        1.5,
        1.6,
        1.7,
        1.75,
        1.8,
        1.9,
        2.0,
        2.5,
        3.0,
        4.0,
        5.0,
        6.0,
        7.0,
        7.5,
        8.0,
        9.0,
    ):
        if scaledValue <= v:
            # print ("computeGoodMax: ", v * (10**decade))
            return v * (10 ** decade)
    # print ("computeGoodMax: ", 10**(decade+1))
    return 10 ** (decade + 1)


def readFiltered(f):
    """Read a non-blank, non-comment line
       N.B. readline returns a zero length line at EOF.
    """
    line = f.readline()
    while line:
        line = line.strip()
        if len(line) != 0 and line[0] != "#":
            return line
        line = f.readline()
    return line


def readFile(fname, desiredFields, minX, maxX):
    """Read data from the file.
    First line gives the test title.
    Second line gives the specific version being run,
    thereafter there is data"""

    print("Reading ", fname)
    with open(fname, "r") as f:
        title = readFiltered(f)
        l = readFiltered(f)
        # print "Title: " + title
        # print "l: " + l

        line = readFiltered(f)
        fieldnames = [x.strip() for x in line.strip().split(",")]
        independentVariable = fieldnames[0]

        # print ("Fieldnames: " + str(fieldnames))
        desiredFields = [independentVariable] + list(
            set(desiredFields) & set(fieldnames)
        )
        selectedFields = computeSelection(fieldnames, desiredFields)
        # print ("selectedFields: " + str(fieldnames))

        results = []
        for line in f:
            line = line[:-1]
            if (not line) or line[0] == "#":
                continue  # Ignore blank lines and comments
            else:
                data = line.split(",")
                try:
                    data = [data[i] for i in selectedFields]
                    # We don't actually have times, but this should still be OK
                    (values, units) = list(zip(*[extractSI(x) for x in data]))
                except:
                    print("*** " + line)
                    continue
                # print "maxX:", maxX, "Values[0] ", values[0]
                if units[0] and (values[0] > maxX or values[0] < minX):
                    # print ("Ignoring ", values[0], " since it's out of range (",minX,":",maxX,")")
                    continue
                values = [values[0]] + [x / scaleFactor for x in values[1:]]
                stat = measurement(desiredFields, values, units)
                results += [stat]

    if not any(["SD" in x for x in desiredFields]):
        results = computeStats(results, independentVariable)

    results = addOverallMeans(
        results,
        desiredFields,
        [stat for stat in desiredFields if stat != independentVariable],
    )

    # Convert the list into a hash by the name of the first column
    byThread = {}
    for v in results:
        byThread[v.__dict__[independentVariable]] = v

    return (title, independentVariable, l, byThread)


def normalizeResults(results, independentVariable, basis):
    """Find each set of results with the same first parameter, compute the 
       min of their means and then scale all their results by that.
       The aim here is to scale results for specific processors in a way that
       is internally consistent. Scaling each run by its own min can be misleading,
       since then results which were smaller can look larger when compared
       with others for the same processor where the min was different but the 
       range larger."""
    normValues = {}
    if basis == "min":
        reduction = min
    elif basis == "mean":
        reduction = mean
    elif basis == "max":
        reduction = max

    print("Normalising by " + basis)
    for k in results.keys():
        ik = k.split(",")[0]
        if ik not in normValues.keys():
            normValues[ik] = []
        values = []
        for iv in results[k].keys():
            values += [results[k][iv].__dict__["Mean"]]
        normValues[ik] += values
    for ik in normValues.keys():
        normValues[ik] = reduction(normValues[ik])

    # And now scale everything
    for k in results.keys():
        ik = k.split(",")[0]
        norm = normValues[ik]
        experiment = results[k]
        for line in experiment.values():
            for value in line.__dict__.keys():
                if value == independentVariable:
                    continue
                line.__dict__[value] = line.__dict__[value] / norm


def highIsBest(metric):
    return "Highest" if ((metric == "rate") or (metric == "speedup")) else "Lowest"


def cleanFilename(fname):
    """Turn runs of bad characters to have in a filename into a single underscore,
    remove any trailing underscore"""
    return re.sub("_$", "", re.sub("[ _\n\t/()*,&:;@.]+", "_", fname))


def generateReport():
    # Parameters of this experiment. Are we looking at a time or a rate?
    # What statistics from the experiment do we want to look at?
    # Do we want the speedup and efficiency curves, or just the raw data?
    metric = "time"
    desiredFields = (
        "Time",
        "Time_SD",
        "Min",
        "Max",
        "Mean",
        "SD",
        "Overhead",
        "Overhead_SD",
        "Efficiency",
        "Number",
        "Maximum Serial",
    )

    from optparse import OptionParser

    global outputDir, htmlFile, implementationStyles

    options = OptionParser()
    options.add_option(
        "--output",
        action="store",
        type="string",
        dest="output",
        default=".",
        help="Directory into which to write the output file",
    )
    options.add_option(
        "--log",
        action="store_true",
        dest="log",
        default=False,
        help="Logarithmic X axis, rather than linear",
    )
    options.add_option(
        "--minmax",
        action="store_true",
        dest="minmax",
        default=False,
        help="Plot min and max error bars rather than standard deviation",
    )
    options.add_option(
        "--noerrs",
        action="store_true",
        dest="noerrs",
        default=False,
        help="Don't plot any form of error bar",
    )
    options.add_option(
        "--fit",
        action="store_true",
        dest="dofit",
        default=False,
        help="Compute linear fit",
    )
    options.add_option(
        "--maxX",
        action="store",
        type="int",
        dest="maxX",
        default=1e20,
        help="Drop values of independent variable above this",
    )
    options.add_option(
        "--minX",
        action="store",
        type="int",
        dest="minX",
        default=None,
        help="Drop values of independent variable below this",
    )
    options.add_option(
        "--maxY",
        action="store",
        type="float",
        dest="maxY",
        default=None,
        help="Fix top of Y axis",
    )
    options.add_option(
        "--noMinY", action="store_true", default=False, help="Don't force minY"
    )
    options.add_option(
        "--speedup",
        action="store_true",
        dest="wantSpeedups",
        default=False,
        help="Add speedups",
    )
    options.add_option(
        "--rate",
        action="store_true",
        dest="rates",
        default=False,
        help="Readings are rates not times",
    )
    options.add_option(
        "--clomp",
        action="store_true",
        dest="clomp",
        default=False,
        help="Read CLOMP speedup",
    )
    options.add_option(
        "--throughput",
        action="store_true",
        dest="throughput",
        default=False,
        help="Compute throughput",
    )
    options.add_option(
        "--exclusive",
        action="store_true",
        dest="exclusive",
        default=False,
        help="Compute exclusive time (1/throughput)",
    )
    options.add_option(
        "--normalize",
        action="store",
        dest="normalize",
        default=None,
        help="Normalize results by first parameter (min, mean, or max)",
    )
    options.add_option(
        "--yName", action="store", dest="yName", default=None, help="Name for Y-axis"
    )
    options.add_option(
        "--mono",
        action="store_true",
        dest="monotone",
        default=False,
        help="Generate black&white plots",
    )
    options.add_option(
        "--legendPos",
        action="store",
        dest="legendPos",
        default="best",
        help="Place the legend, by default 'best', 'below' or 'right' are out of the plot, other values as in matplotlib documentation",
    )
    (options, args) = options.parse_args()

    maxX = options.maxX
    minX = options.minX
    forceMinY = not options.noMinY
    wantSpeedups = options.wantSpeedups
    if options.rates:
        metric = "rate"
        desiredFields = ("Rate", "Rate_SD", "Min", "Max", "Mean", "SD")

    if options.clomp:
        metric = "speedup"
        desiredFields = ("Speedup", "Speedup_SD")

    if options.normalize and not options.normalize in ("min", "mean", "max"):
        print(
            "The normalize option accepts min, mean, or max to choose the normalisation basis"
        )
        return

    if len(args) < 1:
        print("Must give at least one input file")
        sys.exit(1)

    logarithmicX = options.log
    outputDir = options.output
    if not os.path.exists(outputDir):
        os.mkdir(outputDir)

    # Read all the results files
    results = {}
    independentVariable = None
    title = None
    fileList = []
    for a in args:
        selection = glob.glob(a)
        if not selection:
            print("Cannot find files to match " + a)
            continue
        fileList += selection
    for fname in fileList:
        if not os.path.exists(fname):
            # Unlikely; we just globbed this!
            print("Skipping " + fname + " it doesn't exist")
            continue

        (thisTitle, niv, implementation, res) = readFile(
            fname, desiredFields, minX if minX else 0, maxX
        )
        # print thisTitle, " ", niv, " ", implementation
        if title == None:
            title = thisTitle
        elif title != thisTitle:
            print(
                "Files are mismatched, we were looking at "
                + title
                + " but "
                + fname
                + " is for "
                + thisTitle
            )
            sys.exit(1)
        if independentVariable == None:
            independentVariable = niv
        elif independentVariable != niv:
            print(
                "Results were driven by "
                + independentVariable
                + " but new results have "
                + niv
                + " so they are IGNORED"
            )
            sys.exit(1)
            continue
        results[implementation] = res
        sys.stdout.flush()

    if len(list(measurement.units.keys())) == 0:
        print("No data read")
        return

    print("Read " + ",".join(list(measurement.units.keys())))

    if options.normalize:
        title = "Normalized " + title

    # See whether we can move any common parameters from the line and column labels into the title.
    title = moveCommonToTitle(title, results)

    outputFileName = os.path.join(outputDir, cleanFilename(title) + ".html")
    print("Writing output to " + outputFileName)
    htmlFile = open(outputFileName, "w")

    if options.normalize:
        normalizeResults(results, independentVariable, options.normalize)

    # Compute the graphical style for each implementation, so that the same implementation
    # gets the same style in all plots
    # Add in "best fit" cases too in case we want to plot those as well
    implementationStyles = computeStyles(list(results.keys()), options.monotone)

    outputHtmlFileHeader(title)

    allStats = findAllStats()
    allStats.remove(independentVariable)

    print(
        "Available measurements : " + (", ".join(sorted(list(allStats), key=str.lower)))
    )

    if options.minmax:
        minName = "Min"
        maxName = "Max"
    else:
        minName = None
        maxName = None

    transposedResults = transpose(results)

    # Output some interesting statistics
    if metric == "speedup":
        stats = ("Speedup",)
    else:
        stats = (
            "Time",
            "Min",
            "Mean",
            "Overhead",
            "Efficiency",
            "Number",
            "Maximum Serial",
        )
    for stat in stats:
        if stat not in allStats:
            # print "Cannot report on " + stat + ": available stats are " + (", ".join(allStats))
            continue

        thisTitle = title + ": " + stat if needStat(stat, title) else title
        outputHtmlTitle(thisTitle)

        okPercent = 5
        bestCol = outputHtmlTable(
            independentVariable,
            transposedResults,
            lambda x: x.formatValue(stat),
            lambda x: x.getValue(stat),
            highIsBest(metric),
            okPercent=okPercent,
        )

        outputHtml(
            "Best values in red, within " + str(okPercent) + "% have green background."
        )
        if bestCol:
            outputHtml(" Best : '" + bestCol + "'<br>")

        (threadCounts, totalTimes, totalSds) = summarizeResults(
            results,
            stat,
            discard=("Overall Geometric Mean", "Overall Mean"),
            minName=minName,
            maxName=maxName,
        )
        # print ("threadCounts: ",threadCounts)
        # print(stat + ": Units " + measurement.units[stat])
        (unit, timeUnit) = {
            "Time": ("Time(ticks)", "T"),
            "Seconds": ("Time", "s"),
            "Count": ("Count", ""),
            "Rate": ("Ops/T", "T"),
            "Percentage": (stat, "%"),
        }[measurement.units[stat]]
        print("Unit: ", measurement.units[stat], " TimeUnit: ", timeUnit)

        if options.yName:
            unit = options.yName
        elif stat == "Speedup":
            unit = "Speedup"

        if options.normalize:
            unit = unit + " / " + options.normalize + "(per micro-architecture)"

        if options.dofit:
            linearFit(thisTitle, threadCounts, totalTimes, independentVariable, unit)

        yMax = options.maxY
        if yMax == None:
            yMax = computeGoodMax(totalTimes, options.noerrs)
            # print ("Unit:", unit," yMax: ",yMax)
        # if timeUnit in ("%", "Percent") and yMax <= 100.0:
        #     yMax = 100.0

        if options.noerrs:
            totalSds = None

        # print("Title: '" + title + "'")

        numLines = len(list(totalTimes.keys()))
        legendPos = options.legendPos
        generatePlot(
            (title + " " + stat) if needStat(title, stat) else title,
            unit,
            threadCounts,
            totalTimes,
            deviations=totalSds,
            xLabel=independentVariable,
            yMin=0.0 if forceMinY else None,
            yMax=yMax,
            xMin=minX,
            legendPos=legendPos,
            logarithmic=logarithmicX,
            timeUnit=timeUnit,
        )

        if options.throughput:
            if "Exclusive" in title:
                metric = "xt"
            throughputFunctions = {
                "xt": lambda x, nt: 1 / x,
                "time": lambda x, nt: nt / x,
                "rate": lambda x, nt: nt * x,
            }
            throughput = transformResults(
                threadCounts, totalTimes, throughputFunctions[metric]
            )
            # It'd be nice to output the data into a table too, but this needs more work...
            # outputHtmlTitle(title + "\nMachine Throughput " + stat)

            # bestCol = outputHtmlTable(independentVariable, throughput,
            #                       lambda x: x.formatValue(stat),
            #                       lambda x: x.getValue(stat),
            #                       True,  okPercent=okPercent)

            # outputHtml("Best values in red, within " + str(okPercent) + "% have green background.")
            # if (bestCol):
            #     outputHtml(" Best : '" + bestCol + "'<br>")

            # (threadCounts, totalTimes, totalSds) = summarizeResults (results, stat,
            #                                                         discard = ("Overall Geometric Mean", "Overall Mean"),
            #                                                         minName=minName, maxName=maxName)

            generatePlot(
                title + " Machine Throughput " + stat,
                ("Normalized " if options.normalize else "") + "Throughput",
                threadCounts,
                throughput,
                xLabel=independentVariable,
                yMin=0.0 if forceMinY else None,
                # yMax=(100.0 if unit == "%" else computeGoodMax(throughput, False)),
                legendPos=legendPos,
                logarithmic=logarithmicX,
            )

        if options.exclusive:
            exclusiveFunctions = {
                "time": lambda x, nt: x / nt,
                "rate": lambda x, nt: nt * x,
            }
            exclusive = transformResults(
                threadCounts, totalTimes, exclusiveFunctions[metric]
            )
            generatePlot(
                title + "Exclusive Time " + stat,
                "Exclusive Time\nSmaller is Better",
                threadCounts,
                exclusive,
                xLabel=independentVariable,
                xMin=1,
                yMin=0.0 if forceMinY else None,
                yMax=(100.0 if unit == "%" else None),
                legendPos=legendPos,
                logarithmic=logarithmicX,
            )

        if wantSpeedups:
            # Compute speedup and parallel efficiencies relative to the fastest one thread time or rate

            (bestT1, threadCount) = selectedT1(results, stat, metric == "time")
            speedupFunctions = {
                "time": lambda x, nt: bestT1 / x,
                "rate": lambda x, nt: x / bestT1,
            }
            efficiencyFunctions = {
                "time": lambda x, nt: (100 * bestT1) / (x * nt),
                "rate": lambda x, nt: (100 * x) / (bestT1 * nt),
            }

            speedups = transformResults(
                threadCounts, totalTimes, speedupFunctions[metric]
            )

            if threadCount == 1:
                if "Core" in independentVariable:
                    relativeTitle = "relative to best single core"
                else:
                    relativeTitle = "relative to best single thread"
            else:
                relativeTitle = (
                    "relative to P(" + str(threadCount) + ")" + str(threadCount)
                )
            generatePlot(
                title + " Speedup (" + relativeTitle + ")",
                "Speedup",
                threadCounts,
                speedups,
                xLabel=independentVariable,
                xMin=1 if not logarithmicX else 0,
                yMin=0.0 if forceMinY else None,
                yMax=(100.0 if unit == "%" else None),
                legendPos=legendPos,
                logarithmic=logarithmicX,
            )

            efficiencies = transformResults(
                threadCounts, totalTimes, efficiencyFunctions[metric]
            )

            maxEfficiency = max([max(efficiencies[k]) for k in efficiencies.keys()])
            generatePlot(
                title + " Parallel Efficiency (" + relativeTitle + ")",
                "Efficiency (%)",
                threadCounts,
                efficiencies,
                xLabel=independentVariable,
                xMin=1 if not logarithmicX else 0,
                yMin=0.0 if forceMinY else None,
                yMax=100.0 if (maxEfficiency <= 100.0) else None,
                legendPos=legendPos,
                logarithmic=logarithmicX,
            )

    outputHtml("</body> </html>")
    htmlFile.close()


if __name__ == "__main__":
    generateReport()
