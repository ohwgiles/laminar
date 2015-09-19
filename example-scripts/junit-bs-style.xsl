<?xml version="1.0" encoding="utf-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="html" indent="no"/>
<xsl:template match="testsuite">
<h3>Test Suite: <xsl:value-of select="@name" /></h3>
<dl class="dl-horizontal">
 <dt>Tests run:</dt><dd><xsl:value-of select="@tests" /></dd>
 <dt>Failures:</dt><dd><xsl:value-of select="@failures" /></dd>
 <dt>Errors:</dt><dd><xsl:value-of select="@errors" /></dd>
 <dt>Elapsed time:</dt><dd><xsl:value-of select="@time" /></dd>
</dl>
<ul class="list-group">
<xsl:apply-templates select="testcase" />
</ul>
<xsl:apply-templates select="system-out" />
<xsl:apply-templates select="system-err" />
</xsl:template>

<xsl:template match="testcase">
<xsl:choose>
 <xsl:when test="*">
  <li class="list-group-item list-group-item-danger"><xsl:value-of select="@name" />
   <xsl:apply-templates select="failure" />
  </li>
 </xsl:when>
 <xsl:otherwise>
  <li class="list-group-item list-group-item-success"><xsl:value-of select="@name" /></li>
 </xsl:otherwise>
</xsl:choose>
</xsl:template>

<xsl:template match="failure">
<pre>
<xsl:value-of select="@message"/>
</pre>
</xsl:template>

<xsl:template match="system-out"><h5>Standard output:</h5><pre><xsl:value-of select="." /></pre></xsl:template>
<xsl:template match="system-err"><h5>Standard error:</h5><pre><xsl:value-of select="." /></pre></xsl:template>

</xsl:stylesheet>

