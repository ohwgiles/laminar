#!/bin/bash -e

# IMPORTANT: change these to appropriate values, or fetch them, for example
# from the environment or from $(git show -s --format='%ae' $rev)
TO_EMAIL=engineering@example.com
FROM_EMAIL=laminar@example.com

LAMINAR_URL=${LAMINAR_BASE_URL:-http://localhost:8080}

sendmail -t <<EOF
From: $FROM_EMAIL
To: $TO_EMAIL
Subject: $JOB #$RUN: $RESULT
Mime-Version: 1.0
Content-Type: text/plain; charset=utf-8

$(curl -s $LAMINAR_URL/log/$JOB/$RUN)
EOF
