# -*- coding: utf-8; mode: python -*-
# SPDX-License-Identifier: GPL-2.0
# pylint: disable=C0103, R0903, R0912, R0915
"""
    scalable figure and image handling
    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    Sphinx extension which implements scalable image handling.

    :copyright:  Copyright (C) 2016  Markus Heiser
    :license:    GPL Version 2, June 1991 see Linux/COPYING for details.

    The build for image formats depend on image's source format and output's
    destination format. This extension implement methods to simplify image
    handling from the author's POV. Directives like ``kernel-figure`` implement
    methods *to* always get the best output-format even if some tools are not
    installed. For more details take a look at ``convert_image(...)`` which is
    the core of all conversions.

    * ``.. kernel-image``: for image handling / a ``.. image::`` replacement

    * ``.. kernel-figure``: for figure handling / a ``.. figure::`` replacement

    * ``.. kernel-render``: for render markup / a concept to embed *render*
      markups (or languages). Supported markups (see ``RENDER_MARKUP_EXT``)

      - ``DOT``: render embedded Graphviz's **DOC**
      - ``SVG``: render embedded Scalable Vector Graphics (**SVG**)
      - ... *developable*

    Used tools:

    * ``dot(1)``: Graphviz (https://www.graphviz.org). If Graphviz is not
      available, the DOT language is inserted as literal-block.
      For conversion to PDF, ``rsvg-convert(1)`` of librsvg
      (https://gitlab.gnome.org/GNOME/librsvg) is used when available.

    * SVG to PDF: To generate PDF, you need at least one of this tools:

      - ``convert(1)``: ImageMagick (https://www.imagemagick.org)
      - ``inkscape(1)``: Inkscape (https://inkscape.org/)

    List of customizations:

    * generate PDF from SVG / used by PDF (LaTeX) builder

    * generate SVG (html-builder) and PDF (latex-builder) from DOT files.
      DOT: see https://www.graphviz.org/content/dot-language

    """

import os
from os import path
import subprocess
from hashlib import sha1
import re
from docutils import nodes
from docutils.statemachine import ViewList
from docutils.parsers.rst import directives
from docutils.parsers.rst.directives import images
import sphinx
from sphinx.util.nodes import clean_astext
from sphinx.util import logging

Figure = images.Figure

__version__  = '1.0.0'

logger = logging.getLogger('kfigure')

# simple helper
# -------------

def which(cmd):
    """Searches the ``cmd`` in the ``PATH`` environment.

    This *which* searches the PATH for executable ``cmd`` . First match is
    returned, if nothing is found, ``None` is returned.
    """
    envpath = os.environ.get('PATH', None) or os.defpath
    for folder in envpath.split(os.pathsep):
        fname = folder + os.sep + cmd
        if path.isfile(fname):
            return fname

def mkdir(folder, mode=0o775):
    if not path.isdir(folder):
        os.makedirs(folder, mode)

def file2literal(fname):
    with open(fname, "r") as src:
        data = src.read()
        node = nodes.literal_block(data, data)
    return node

def isNewer(path1, path2):
    """Returns True if ``path1`` is newer than ``path2``

    If ``path1`` exists and is newer than ``path2`` the function returns
    ``True`` is returned otherwise ``False``
    """
    return (path.exists(path1)
            and os.stat(path1).st_ctime > os.stat(path2).st_ctime)

def pass_handle(self, node):           # pylint: disable=W0613
    pass

# setup conversion tools and sphinx extension
# -------------------------------------------

# Graphviz's dot(1) support
dot_cmd = None
# dot(1) -Tpdf should be used
dot_Tpdf = False

# ImageMagick' convert(1) support
convert_cmd = None

# librsvg's rsvg-convert(1) support
rsvg_convert_cmd = None

# Inkscape's inkscape(1) support
inkscape_cmd = None
# Inkscape prior to 1.0 uses different command options
inkscape_ver_one = False


def setup(app):
    # check toolchain first
    app.connect('builder-inited', setupTools)

    # image handling
    app.add_directive("kernel-image",  KernelImage)
    app.add_node(kernel_image,
                 html    = (visit_kernel_image, pass_handle),
                 latex   = (visit_kernel_image, pass_handle),
                 texinfo = (visit_kernel_image, pass_handle),
                 text    = (visit_kernel_image, pass_handle),
                 man     = (visit_kernel_image, pass_handle), )

    # figure handling
    app.add_directive("kernel-figure", KernelFigure)
    app.add_node(kernel_figure,
                 html    = (visit_kernel_figure, pass_handle),
                 latex   = (visit_kernel_figure, pass_handle),
                 texinfo = (visit_kernel_figure, pass_handle),
                 text    = (visit_kernel_figure, pass_handle),
                 man     = (visit_kernel_figure, pass_handle), )

    # render handling
    app.add_directive('kernel-render', KernelRender)
    app.add_node(kernel_render,
                 html    = (visit_kernel_render, pass_handle),
                 latex   = (visit_kernel_render, pass_handle),
                 texinfo = (visit_kernel_render, pass_handle),
                 text    = (visit_kernel_render, pass_handle),
                 man     = (visit_kernel_render, pass_handle), )

    app.connect('doctree-read', add_kernel_figure_to_std_domain)

    return dict(
        version = __version__,
        parallel_read_safe = True,
        parallel_write_safe = True
    )


def setupTools(app):
    """
    Check available build tools and log some *verbose* messages.

    This function is called once, when the builder is initiated.
    """
    global dot_cmd, dot_Tpdf, convert_cmd, rsvg_convert_cmd   # pylint: disable=W0603
    global inkscape_cmd, inkscape_ver_one  # pylint: disable=W0603
    logger.verbose("kfigure: check installed tools ...")

    dot_cmd = which('dot')
    convert_cmd = which('convert')
    rsvg_convert_cmd = which('rsvg-convert')
    inkscape_cmd = which('inkscape')

    if dot_cmd:
        logger.verbose("use dot(1) from: " + dot_cmd)

        try:
            dot_Thelp_list = subprocess.check_output([dot_cmd, '-Thelp'],
                                    stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as err:
            dot_Thelp_list = err.output
            pass

        dot_Tpdf_ptn = b'pdf'
        dot_Tpdf = re.search(dot_Tpdf_ptn, dot_Thelp_list)
    else:
        logger.warning(
            "dot(1) not found, for better output quality install graphviz from https://www.graphviz.org"
        )
    if inkscape_cmd:
        logger.verbose("use inkscape(1) from: " + inkscape_cmd)
        inkscape_ver = subprocess.check_output([inkscape_cmd, '--version'],
                                               stderr=subprocess.DEVNULL)
        ver_one_ptn = b'Inkscape 1'
        inkscape_ver_one = re.search(ver_one_ptn, inkscape_ver)
        convert_cmd = None
        rsvg_convert_cmd = None
        dot_Tpdf = False

    else:
        if convert_cmd:
            logger.verbose("use convert(1) from: " + convert_cmd)
        else:
            logger.verbose(
                "Neither inkscape(1) nor convert(1) found.\n"
                "For SVG to PDF conversion, install either Inkscape (https://inkscape.org/) (preferred) or\n"
                "ImageMagick (https://www.imagemagick.org)"
            )

        if rsvg_convert_cmd:
            logger.verbose("use rsvg-convert(1) from: " + rsvg_convert_cmd)
            logger.verbose("use 'dot -Tsvg' and rsvg-convert(1) for DOT -> PDF conversion")
            dot_Tpdf = False
        else:
            logger.verbose(
                "rsvg-convert(1) not found.\n"
                "  SVG rendering of convert(1) is done by ImageMagick-native renderer."
            )
            if dot_Tpdf:
                logger.verbose("use 'dot -Tpdf' for DOT -> PDF conversion")
            else:
                logger.verbose("use 'dot -Tsvg' and convert(1) for DOT -> PDF conversion")


# integrate conversion tools
# --------------------------

RENDER_MARKUP_EXT = {
    # The '.ext' must be handled by convert_image(..) function's *in_ext* input.
    # <name> : <.ext>
    'DOT' : '.dot',
    'SVG' : '.svg'
}

def convert_image(img_node, translator, src_fname=None):
    """Convert a image node for the builder.

    Different builder prefer different image formats, e.g. *latex* builder
    prefer PDF while *html* builder prefer SVG format for images.

    This function handles output image formats in dependence of source the
    format (of the image) and the translator's output format.
    """
    app = translator.builder.app

    fname, in_ext = path.splitext(path.basename(img_node['uri']))
    if src_fname is None:
        src_fname = path.join(translator.builder.srcdir, img_node['uri'])
        if not path.exists(src_fname):
            src_fname = path.join(translator.builder.outdir, img_node['uri'])

    dst_fname = None

    # in kernel builds, use 'make SPHINXOPTS=-v' to see verbose messages

    logger.verbose('assert best format for: ' + img_node['uri'])

    if in_ext == '.dot':

        if not dot_cmd:
            logger.verbose("dot from graphviz not available / include DOT raw.")
            img_node.replace_self(file2literal(src_fname))

        elif translator.builder.format == 'latex':
            dst_fname = path.join(translator.builder.outdir, fname + '.pdf')
            img_node['uri'] = fname + '.pdf'
            img_node['candidates'] = {'*': fname + '.pdf'}


        elif translator.builder.format == 'html':
            dst_fname = path.join(
                translator.builder.outdir,
                translator.builder.imagedir,
                fname + '.svg')
            img_node['uri'] = path.join(
                translator.builder.imgpath, fname + '.svg')
            img_node['candidates'] = {
                '*': path.join(translator.builder.imgpath, fname + '.svg')}

        else:
            # all other builder formats will include DOT as raw
            img_node.replace_self(file2literal(src_fname))

    elif in_ext == '.svg':

        if translator.builder.format == 'latex':
            if not inkscape_cmd and convert_cmd is None:
                logger.warning(
                    "no SVG to PDF conversion available / include SVG raw.\n"
                    "Including large raw SVGs can cause xelatex error.\n"
                    "Install Inkscape (preferred) or ImageMagick."
                )
                img_node.replace_self(file2literal(src_fname))
            else:
                dst_fname = path.join(translator.builder.outdir, fname + '.pdf')
                img_node['uri'] = fname + '.pdf'
                img_node['candidates'] = {'*': fname + '.pdf'}

    if dst_fname:
        # the builder needs not to copy one more time, so pop it if exists.
        translator.builder.images.pop(img_node['uri'], None)
        _name = dst_fname[len(str(translator.builder.outdir)) + 1:]

        if isNewer(dst_fname, src_fname):
            logger.verbose("convert: {out}/%s already exists and is newer" % _name)

        else:
            ok = False
            mkdir(path.dirname(dst_fname))

            if in_ext == '.dot':
                logger.verbose('convert DOT to: {out}/' + _name)
                if translator.builder.format == 'latex' and not dot_Tpdf:
                    svg_fname = path.join(translator.builder.outdir, fname + '.svg')
                    ok1 = dot2format(app, src_fname, svg_fname)
                    ok2 = svg2pdf_by_rsvg(app, svg_fname, dst_fname)
                    ok = ok1 and ok2

                else:
                    ok = dot2format(app, src_fname, dst_fname)

            elif in_ext == '.svg':
                logger.verbose('convert SVG to: {out}/' + _name)
                ok = svg2pdf(app, src_fname, dst_fname)

            if not ok:
                img_node.replace_self(file2literal(src_fname))


def dot2format(app, dot_fname, out_fname):
    """Converts DOT file to ``out_fname`` using ``dot(1)``.

    * ``dot_fname`` pathname of the input DOT file, including extension ``.dot``
    * ``out_fname`` pathname of the output file, including format extension

    The *format extension* depends on the ``dot`` command (see ``man dot``
    option ``-Txxx``). Normally you will use one of the following extensions:

    - ``.ps`` for PostScript,
    - ``.svg`` or ``svgz`` for Structured Vector Graphics,
    - ``.fig`` for XFIG graphics and
    - ``.png`` or ``gif`` for common bitmap graphics.

    """
    out_format = path.splitext(out_fname)[1][1:]
    cmd = [dot_cmd, '-T%s' % out_format, dot_fname]
    exit_code = 42

    with open(out_fname, "w") as out:
        exit_code = subprocess.call(cmd, stdout = out)
        if exit_code != 0:
            logger.warning(
                          "Error #%d when calling: %s" % (exit_code, " ".join(cmd)))
    return bool(exit_code == 0)

def svg2pdf(app, svg_fname, pdf_fname):
    """Converts SVG to PDF with ``inkscape(1)`` or ``convert(1)`` command.

    Uses ``inkscape(1)`` from Inkscape (https://inkscape.org/) or ``convert(1)``
    from ImageMagick (https://www.imagemagick.org) for conversion.
    Returns ``True`` on success and ``False`` if an error occurred.

    * ``svg_fname`` pathname of the input SVG file with extension (``.svg``)
    * ``pdf_name``  pathname of the output PDF file with extension (``.pdf``)

    """
    cmd = [convert_cmd, svg_fname, pdf_fname]
    cmd_name = 'convert(1)'

    if inkscape_cmd:
        cmd_name = 'inkscape(1)'
        if inkscape_ver_one:
            cmd = [inkscape_cmd, '-o', pdf_fname, svg_fname]
        else:
            cmd = [inkscape_cmd, '-z', '--export-pdf=%s' % pdf_fname, svg_fname]

    try:
        warning_msg = subprocess.check_output(cmd, stderr=subprocess.STDOUT)
        exit_code = 0
    except subprocess.CalledProcessError as err:
        warning_msg = err.output
        exit_code = err.returncode
        pass

    if exit_code != 0:
        logger.warning("Error #%d when calling: %s" %
                            (exit_code, " ".join(cmd)))
        if warning_msg:
            logger.warning( "Warning msg from %s: %s" %
                                (cmd_name, str(warning_msg, 'utf-8')))
    elif warning_msg:
        logger.verbose("Warning msg from %s (likely harmless):\n%s" %
                            (cmd_name, str(warning_msg, 'utf-8')))

    return bool(exit_code == 0)

def svg2pdf_by_rsvg(app, svg_fname, pdf_fname):
    """Convert SVG to PDF with ``rsvg-convert(1)`` command.

    * ``svg_fname`` pathname of input SVG file, including extension ``.svg``
    * ``pdf_fname`` pathname of output PDF file, including extension ``.pdf``

    Input SVG file should be the one generated by ``dot2format()``.
    SVG -> PDF conversion is done by ``rsvg-convert(1)``.

    If ``rsvg-convert(1)`` is unavailable, fall back to ``svg2pdf()``.

    """

    if rsvg_convert_cmd is None:
        ok = svg2pdf(app, svg_fname, pdf_fname)
    else:
        cmd = [rsvg_convert_cmd, '--format=pdf', '-o', pdf_fname, svg_fname]
        # use stdout and stderr from parent
        exit_code = subprocess.call(cmd)
        if exit_code != 0:
            logger.warning("Error #%d when calling: %s" %
                                (exit_code, " ".join(cmd)))
        ok = bool(exit_code == 0)

    return ok


# image handling
# ---------------------

def visit_kernel_image(self, node):    # pylint: disable=W0613
    """Visitor of the ``kernel_image`` Node.

    Handles the ``image`` child-node with the ``convert_image(...)``.
    """
    img_node = node[0]
    convert_image(img_node, self)

class kernel_image(nodes.image):
    """Node for ``kernel-image`` directive."""
    pass

class KernelImage(images.Image):
    """KernelImage directive

    Earns everything from ``.. image::`` directive, except *remote URI* and
    *glob* pattern. The KernelImage wraps a image node into a
    kernel_image node. See ``visit_kernel_image``.
    """

    def run(self):
        uri = self.arguments[0]
        if uri.endswith('.*') or uri.find('://') != -1:
            raise self.severe(
                'Error in "%s: %s": glob pattern and remote images are not allowed'
                % (self.name, uri))
        result = images.Image.run(self)
        if len(result) == 2 or isinstance(result[0], nodes.system_message):
            return result
        (image_node,) = result
        # wrap image node into a kernel_image node / see visitors
        node = kernel_image('', image_node)
        return [node]

# figure handling
# ---------------------

def visit_kernel_figure(self, node):   # pylint: disable=W0613
    """Visitor of the ``kernel_figure`` Node.

    Handles the ``image`` child-node with the ``convert_image(...)``.
    """
    img_node = node[0][0]
    convert_image(img_node, self)

class kernel_figure(nodes.figure):
    """Node for ``kernel-figure`` directive."""

class KernelFigure(Figure):
    """KernelImage directive

    Earns everything from ``.. figure::`` directive, except *remote URI* and
    *glob* pattern.  The KernelFigure wraps a figure node into a kernel_figure
    node. See ``visit_kernel_figure``.
    """

    def run(self):
        uri = self.arguments[0]
        if uri.endswith('.*') or uri.find('://') != -1:
            raise self.severe(
                'Error in "%s: %s":'
                ' glob pattern and remote images are not allowed'
                % (self.name, uri))
        result = Figure.run(self)
        if len(result) == 2 or isinstance(result[0], nodes.system_message):
            return result
        (figure_node,) = result
        # wrap figure node into a kernel_figure node / see visitors
        node = kernel_figure('', figure_node)
        return [node]


# render handling
# ---------------------

def visit_kernel_render(self, node):
    """Visitor of the ``kernel_render`` Node.

    If rendering tools available, save the markup of the ``literal_block`` child
    node into a file and replace the ``literal_block`` node with a new created
    ``image`` node, pointing to the saved markup file. Afterwards, handle the
    image child-node with the ``convert_image(...)``.
    """
    app = self.builder.app
    srclang = node.get('srclang')

    logger.verbose('visit kernel-render node lang: "%s"' % srclang)

    tmp_ext = RENDER_MARKUP_EXT.get(srclang, None)
    if tmp_ext is None:
        logger.warning( 'kernel-render: "%s" unknown / include raw.' % srclang)
        return

    if not dot_cmd and tmp_ext == '.dot':
        logger.verbose("dot from graphviz not available / include raw.")
        return

    literal_block = node[0]

    code      = literal_block.astext()
    hashobj   = code.encode('utf-8') #  str(node.attributes)
    fname     = path.join('%s-%s' % (srclang, sha1(hashobj).hexdigest()))

    tmp_fname = path.join(
        self.builder.outdir, self.builder.imagedir, fname + tmp_ext)

    if not path.isfile(tmp_fname):
        mkdir(path.dirname(tmp_fname))
        with open(tmp_fname, "w") as out:
            out.write(code)

    img_node = nodes.image(node.rawsource, **node.attributes)
    img_node['uri'] = path.join(self.builder.imgpath, fname + tmp_ext)
    img_node['candidates'] = {
        '*': path.join(self.builder.imgpath, fname + tmp_ext)}

    literal_block.replace_self(img_node)
    convert_image(img_node, self, tmp_fname)


class kernel_render(nodes.General, nodes.Inline, nodes.Element):
    """Node for ``kernel-render`` directive."""
    pass

class KernelRender(Figure):
    """KernelRender directive

    Render content by external tool.  Has all the options known from the
    *figure*  directive, plus option ``caption``.  If ``caption`` has a
    value, a figure node with the *caption* is inserted. If not, a image node is
    inserted.

    The KernelRender directive wraps the text of the directive into a
    literal_block node and wraps it into a kernel_render node. See
    ``visit_kernel_render``.
    """
    has_content = True
    required_arguments = 1
    optional_arguments = 0
    final_argument_whitespace = False

    # earn options from 'figure'
    option_spec = Figure.option_spec.copy()
    option_spec['caption'] = directives.unchanged

    def run(self):
        return [self.build_node()]

    def build_node(self):

        srclang = self.arguments[0].strip()
        if srclang not in RENDER_MARKUP_EXT.keys():
            return [self.state_machine.reporter.warning(
                'Unknown source language "%s", use one of: %s.' % (
                    srclang, ",".join(RENDER_MARKUP_EXT.keys())),
                line=self.lineno)]

        code = '\n'.join(self.content)
        if not code.strip():
            return [self.state_machine.reporter.warning(
                'Ignoring "%s" directive without content.' % (
                    self.name),
                line=self.lineno)]

        node = kernel_render()
        node['alt'] = self.options.get('alt','')
        node['srclang'] = srclang
        literal_node = nodes.literal_block(code, code)
        node += literal_node

        caption = self.options.get('caption')
        if caption:
            # parse caption's content
            parsed = nodes.Element()
            self.state.nested_parse(
                ViewList([caption], source=''), self.content_offset, parsed)
            caption_node = nodes.caption(
                parsed[0].rawsource, '', *parsed[0].children)
            caption_node.source = parsed[0].source
            caption_node.line = parsed[0].line

            figure_node = nodes.figure('', node)
            for k,v in self.options.items():
                figure_node[k] = v
            figure_node += caption_node

            node = figure_node

        return node

def add_kernel_figure_to_std_domain(app, doctree):
    """Add kernel-figure anchors to 'std' domain.

    The ``StandardDomain.process_doc(..)`` method does not know how to resolve
    the caption (label) of ``kernel-figure`` directive (it only knows about
    standard nodes, e.g. table, figure etc.). Without any additional handling
    this will result in a 'undefined label' for kernel-figures.

    This handle adds labels of kernel-figure to the 'std' domain labels.
    """

    std = app.env.domains["std"]
    docname = app.env.docname
    labels = std.data["labels"]

    for name, explicit in doctree.nametypes.items():
        if not explicit:
            continue
        labelid = doctree.nameids[name]
        if labelid is None:
            continue
        node = doctree.ids[labelid]

        if node.tagname == 'kernel_figure':
            for n in node.next_node():
                if n.tagname == 'caption':
                    sectname = clean_astext(n)
                    # add label to std domain
                    labels[name] = docname, labelid, sectname
                    break
