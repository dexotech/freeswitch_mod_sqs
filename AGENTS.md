# AGENTS.md

## Overview

Please err on the side of high quality, not lazy, implementation decisions, because the code
will have to be maintained for a long time.  Everybody, LLM or person, is able to work
better if we keep the code clean and to a high standard to start with.

Even if your instructions don't include specific admonishment about quality, it is always
necessary. Shortcuts and desperate incomplete hacks to deliver the feature are always wrong.

Our work should follow the existing usage of apis in the project or related libraries and
projects as much as possible.

## Interacting

Always work off of feature branches, and create PRs when feature is delivered. Consolidate commits
as much as possible, do a final review pass and squash commits where it makes sense. No commit should
include multiple features or too many changes, commits should be concise and clean.

Often although we are working on the same sources, they are being tested on devices you don't have
access to. So you must ask for access to data state on those remote machines; looking at the local
machine you are running on for config or data state directly is of zero use in those circumstances.

## Completeness

If you are unable to complete something your coding partner expects from the interaction
with you, you must clearly explain to the user which parts are incomplete in this phase and need
further work. DO NOT leave it silently incomplete and act like it is done without making the
situation crystal clear to your partner.

Often adding / modifying or removing features has a very strong expectation that you will
also take responsibility about certain side-effects. If we add significant new code, it must
also bring with it api-test or other example code to confirm it works properly.

## Example code

The examples are not chaotic dumping grounds for trash.  They are supposed to show the user
the best way we know how to do things, that they can use in their own code reliably.  We
should make an extra effort to keep them clean and as quality exemplars.

## C Coding

We are very concerned about security, architecturally and in the code. We avoid using:

 - things like `scanf` for carefully parsing with code.

 - `FILE *`: use apis like open(), read().

 - hand-rolled ad-hoc code when there are library helpers, eg no casual
   linked-lists or primitive lists.

 - We NEVER INSTALL OR USE known attack vectors like node.js or any other
   platform with uncontrolled remote finegrained dependencies, even on a VM.

 - Please don't populate changelog. It's for the highest level changes only.

## Security

Please bear in mind:

 - which parts of the system are secrets, and look after the security of them.
 - all external data is untrusted and should be assumed to be part of an attack until
   we have validated it to be within a range and type we expect and can safely consume.

 - all web pieces are served with a strict CSP.  That means ** no inline styles or scripts ** .
   You can usually find the web pieces (JS, HTML, css) in ./assets/

 - Before a vulnerability is found, you would have failed to take care about it. Keep in mind
   common vulnerability patterns and avoid them in new code so nobody has to find your vuln.

 - Coverity, SonarQube and CodeQL might be looking at your changes. Please avoid creating
   situations they will flag.

## Build testing

If we are adding code to any library, if it's anything nontrivial we should think about adding an api test
or minimal example.
