#!/bin/bash -e

# IMPORTANT: change these to appropriate values, or fetch them, for example
# from the environment or from $(git show -s --format='%ae' $rev)
TO_EMAIL=engineering@example.com
FROM_EMAIL=laminar@example.com

LAMINAR_URL=${LAMINAR_BASE_URL:-http://localhost:8080}
LAMINAR_TITLE=${LAMINAR_TITLE:-Laminar CI}

if [[ $RESULT = "success" ]]; then
 SVGICON=$(cat <<-EOF
	<svg viewBox="0 0 100 100" width="24px">
	<path fill="#74af77" d="m 23,46 c -6,0 -17,3 -17,11 0,8 9,30 12,32 3,2 14,5 20,-2 6,-6 24,-36
	                          56,-71 5,-3 -9,-8 -23,-2 -13,6 -33,42 -41,47 -6,-3 -5,-12 -8,-15 z" />
	</svg>
	EOF
 )
else
 SVGICON=$(cat <<-EOF
	<svg viewBox="0 0 100 100" width="24px">
	<path fill="#883d3d" d="m 19,20 c 2,8 12,29 15,32 -5,5 -18,21 -21,26 2,3 8,15 11,18 4,-6 17,-21
	           21,-26 5,5 11,15 15,20 8,-2 15,-9 20,-15 -3,-3 -17,-18 -20,-24 3,-5 23,-26 30,-33 -3,-5 -8,-9
	           -12,-12 -6,5 -26,26 -29,30 -6,-8 -11,-15 -15,-23 -3,0 -12,5 -15,7 z" />
	</svg>
	EOF
 )
fi

sendmail -t <<EOF
From: $FROM_EMAIL
To: $TO_EMAIL
Subject: $JOB #$RUN: $RESULT
Mime-Version: 1.0
Content-Type: text/html; charset=utf-8

<html lang="en">
 <body bgcolor="#efefef" style="margin: 0; font-family: Helvetica Neue, Helvetica, Arial, sans-serif">
  <table width="100%" border="0" cellspacing="0" cellpadding="0">
   <tr><td align="center">
    <table border="0" cellspacing="0" cellpadding="15" bgcolor="#ffffff">
     <tr bgcolor="#2f3340">
      <td style="font-size: 28px; color: #ffffff;">$LAMINAR_TITLE</td></tr>
     <tr>
      <td style="font-size: 26px">
       $SVGICON
       <a href="$LAMINAR_URL/jobs/$JOB/$RUN">$JOB #$RUN</a>
      </td>
     </tr>
    </table>
   </td></tr>
  </table>
 </body>
</html>
EOF
