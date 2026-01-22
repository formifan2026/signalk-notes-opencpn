#! /bin/bash

# This script refactors this plugin into a new plugin with a name of your choice.
# To rename the plugin to "catsarebest", run `bash make-new-plugin.sh catsarebest`.

newname="$1"
Newname="$(echo "${newname^}")"
NEWNAME="$(echo "${newname^^}")"

grep -rl signalk_notes_opencpn . | grep -v .git | while read name; do
  sed -e "s+signalk_notes_opencpn+$newname+g" -i "$name";
done

grep -rl Signalk_notes_opencpn . | grep -v .git | while read name; do  
  sed -e "s+Signalk_notes_opencpn+$Newname+g" -i "$name";
done 

grep -rl SIGNALK_NOTES_OPENCPN . | grep -v .git | while read name; do  
  sed -e "s+SIGNALK_NOTES_OPENCPN+$NEWNAME+g" -i "$name";
done 

find . -name "*signalk_notes_opencpn*" | grep -v .git | while read name; do
  mv "$name" "$(echo "$name" | sed -e "s+signalk_notes_opencpn+$newname+g")"
done

find . -name "*Signalk_notes_opencpn*" | grep -v .git | while read name; do
  mv "$name" "$(echo "$name" | sed -e "s+Signalk_notes_opencpn+$Newname+g")"
done
