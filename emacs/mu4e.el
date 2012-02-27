;;; mu4e.el -- part of mu4e, the mu mail user agent
;;
;; Copyright (C) 2011 Dirk-Jan C. Binnema

;; Author: Dirk-Jan C. Binnema <djcb@djcbsoftware.nl>
;; Maintainer: Dirk-Jan C. Binnema <djcb@djcbsoftware.nl>
;; Keywords: email
;; Version: 0.0

;; This file is not part of GNU Emacs.
;;
;; GNU Emacs is free software: you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation, either version 3 of the License, or
;; (at your option) any later version.

;; GNU Emacs is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GNU Emacs.  If not, see <http://www.gnu.org/licenses/>.

;;; Commentary:

;;; Code:

(eval-when-compile (require 'cl))

(require 'mu4e-hdrs)
(require 'mu4e-view)
(require 'mu4e-main)
(require 'mu4e-send)
(require 'mu4e-proc)

;; mu4e-version.el is autogenerated, and defines mu4e-mu-version
(require 'mu4e-version)

;; Customization

(defgroup mu4e nil
  "mu4e - mu for emacs"
  :group 'local)

(defcustom mu4e-mu-home nil
  "Location of the mu homedir, or nil for the default."
  :type 'directory
  :group 'mu4e
  :safe 'stringp)

(defcustom mu4e-mu-binary (executable-find "mu")
  "Name of the mu-binary to use; if it cannot be found in your
PATH, you can specify the full path."
  :type 'file
  :group 'mu4e
  :safe 'stringp)

(defcustom mu4e-maildir (expand-file-name "~/Maildir")
  "Your Maildir directory; by default, mu4e assumes ~/Maildir."
  :type 'directory
  :safe 'stringp
  :group 'mu4e)

(defcustom mu4e-get-mail-command nil
  "Shell command to run to retrieve new mail; e.g. 'offlineimap' or
'fetchmail'."
  :type 'string
  :group 'mu4e
  :safe 'stringp)

(defcustom mu4e-attachment-dir (expand-file-name "~/")
  "Default directory for saving attachments."
  :type 'string
  :group 'mu4e
  :safe 'stringp)

(defvar mu4e-user-mail-address-regexp "$^"
  "Regular expression matching the user's mail address(es). This is
used to distinguish ourselves from others, e.g. when replying and
in :from-or-to headers. By default, match nothing.")

(defvar mu4e-date-format-long "%c"
  "Date format to use in the message view, in the format of
  `format-time-string'.")

(defvar mu4e-search-results-limit 500
  "Maximum number of search results (or -1 for unlimited). Since
limiting search results speeds up searches significantly, it's
useful to limit this. Note, to ignore the limit, use a prefix
argument (C-u) before invoking the search.")


(defvar mu4e-debug nil
  "When set to non-nil, log debug information to the *mu4e-log* buffer.")

(defvar mu4e-bookmarks
  '( ("flag:unread AND NOT flag:trashed" "Unread messages"      ?u)
     ("date:today..now"                  "Today's messages"     ?t)
     ("date:7d..now"                     "Last 7 days"          ?w)
     ("mime:image/*"                     "Messages with images" ?p))
  "A list of pre-defined queries; these will show up in the main
screen. Each of the list elements is a three-element list of the
form (QUERY DESCRIPTION KEY), where QUERY is a string with a mu
query, DESCRIPTION is a short description of the query (this will
show up in the UI), and KEY is a shortcut key for the query.")


;; Folders

(defgroup mu4e-folders nil
  "Special folders for mm."
  :group 'mu4e)

(defcustom mu4e-sent-folder "/sent"
  "Your folder for sent messages, relative to `mu4e-maildir',
  e.g. \"/Sent Items\"."
  :type 'string
  :safe 'stringp
  :group 'mu4e-folders)

(defcustom mu4e-drafts-folder "/drafts"
  "Your folder for draft messages, relative to `mu4e-maildir',
  e.g. \"/drafts\""
  :type 'string
  :safe 'stringp
  :group 'mu4e-folders)

(defcustom mu4e-trash-folder "/trash"
  "Your folder for trashed messages, relative to `mu4e-maildir',
  e.g. \"/trash\"."
  :type 'string
  :safe 'stringp
  :group 'mu4e-folders)


(defcustom mu4e-maildir-shortcuts nil
  "A list of maildir shortcuts to enable quickly going to the
 particular for, or quickly moving messages towards them (i.e.,
 archiving or refiling). The list contains elements of the form
 (maildir . shortcut), where MAILDIR is a maildir (such as
\"/archive/\"), and shortcut a single shortcut character. With
this, in the header buffer and view buffer you can execute
`mu4e-mark-for-move-quick' (or 'm', by default) or
`mu4e-jump-to-maildir' (or 'j', by default), followed by the
designated shortcut character for the maildir.")

;; the headers view
(defgroup mu4e-headers nil
  "Settings for the headers view."
  :group 'mu4e)


(defcustom mu4e-headers-fields
    '( (:date          .  25)
       (:flags         .   6)
       (:from          .  22)
       (:subject       .  nil))
  "A list of header fields to show in the headers buffer, and their
  respective widths in characters. A width of `nil' means
  'unrestricted', and this is best reserved fo the rightmost (last)
  field. For the complete list of available headers, see `mu4e-header-names'"
       :type (list 'symbol)
       :group 'mu4e-headers)

(defcustom mu4e-headers-date-format "%x %X"
  "Date format to use in the headers view, in the format of
  `format-time-string'."
  :type  'string
  :group 'mu4e-headers)


;; the message view
(defgroup mu4e-view nil
  "Settings for the message view."
  :group 'mu4e)

(defcustom mu4e-view-fields
  '(:from :to :cc :subject :flags :date :maildir :path :attachments)
  "Header fields to display in the message view buffer. For the
complete list of available headers, see `mu4e-header-names'."
  :type (list 'symbol)
  :group 'mu4e-view)

(defcustom mu4e-view-date-format "%c"
  "Date format to use in the message view, in the format of
  `format-time-string'."
  :type 'string
  :group 'mu4e-view)

(defcustom mu4e-view-prefer-html nil
  "Whether to base the body display on the HTML-version of the
e-mail message (if there is any."
  :type 'boolean
  :group 'mu4e-view)

(defcustom mu4e-html2text-command nil
  "Shel command that converts HTML from stdin into plain text on
stdout. If this is not defined, the emacs `html2text' tool will be
used when faced with html-only message. If you use htmltext, it's
recommended you use \"html2text -utf8 -width 72\"."
  :type 'string
  :group 'mu4e-view
  :safe 'stringp)

;; Composing / Sending messages
(defgroup mu4e-compose nil
  "Customizations for composing/sending messages."
  :group 'mu4e)

(defcustom mu4e-send-citation-prefix "> "
  "String to prefix cited message parts with."
  :type 'string
  :group 'mu4e-compose)

(defcustom mu4e-send-reply-prefix "Re: "
  "String to prefix the subject of replied messages with."
  :type 'string
  :group 'mu4e-compose)

(defcustom mu4e-send-forward-prefix "Fwd: "
  "String to prefix the subject of forwarded messages with."
  :type 'string
  :group 'mu4e-compose)

(defcustom mu4e-user-agent nil
  "The user-agent string; leave at `nil' for the default."
  :type 'string
  :group 'mu4e-compose)

;; Faces

(defgroup mu4e-faces nil
  "Faces used in by mm."
  :group 'mu4e
  :group 'faces)

(defface mu4e-unread-face
  '((t :inherit font-lock-keyword-face :bold t))
  "Face for an unread mm message header."
  :group 'mu4e-faces)

(defface mu4e-moved-face
  '((t :inherit font-lock-comment-face :slant italic))
  "Face for an mm message header that has been moved to some
folder (it's still visible in the search results, since we cannot
be sure it no longer matches)."
  :group 'mu4e-faces)

(defface mu4e-trashed-face
  '((t :inherit font-lock-comment-face :strike-through t))
  "Face for an message header in the trash folder."
  :group 'mu4e-faces)

(defface mu4e-draft-face
  '((t :inherit font-lock-string-face))
  "Face for a draft message header (i.e., a message with the draft
flag set)."
  :group 'mu4e-faces)

(defface mu4e-header-face
  '((t :inherit default))
  "Face for an mm header without any special flags."
  :group 'mu4e-faces)

(defface mu4e-title-face
  '((t :inherit font-lock-type-face))
  "Face for an mm title."
  :group 'mu4e-faces)

(defface mu4e-view-header-key-face
  '((t :inherit font-lock-builtin-face :bold t))
  "Face for the header title (such as \"Subject\" in the message view)."
  :group 'mu4e-faces)

(defface mu4e-view-header-value-face
  '((t :inherit font-lock-doc-face))
  "Face for the header value (such as \"Re: Hello!\" in the message view)."
  :group 'mu4e-faces)

(defface mu4e-view-link-face
  '((t :inherit font-lock-type-face :underline t))
  "Face for showing URLs and attachments in the message view."
  :group 'mu4e-faces)

(defface mu4e-highlight-face
  '((t :inherit font-lock-pseudo-keyword-face :bold t))
  "Face for highlighting things."
  :group 'mu4e-faces)

(defface mu4e-view-url-number-face
  '((t :inherit font-lock-reference-face :bold t))
  "Face for the number tags for URLs."
  :group 'mu4e-faces)

(defface mu4e-view-attach-number-face
  '((t :inherit font-lock-variable-name-face :bold t))
  "Face for the number tags for attachments."
  :group 'mu4e-faces)

(defface mu4e-cited-1-face
  '((t :inherit font-lock-builtin-face :bold nil :italic t))
  "Face for cited message parts (level 1)."
  :group 'mu4e-faces)

(defface mu4e-cited-2-face
  '((t :inherit font-lock-type-face :bold nil :italic t))
  "Face for cited message parts (level 2)."
  :group 'mu4e-faces)

(defface mu4e-cited-3-face
  '((t :inherit font-lock-variable-name-face :bold nil :italic t))
  "Face for cited message parts (level 3)."
  :group 'mu4e-faces)

(defface mu4e-cited-4-face
  '((t :inherit font-lock-pseudo-keyword-face :bold nil :italic t))
  "Face for cited message parts (level 4)."
  :group 'mu4e-faces)

(defface mu4e-view-footer-face
  '((t :inherit font-lock-comment-face))
  "Face for message footers (signatures)."
  :group 'mu4e-faces)

(defface mu4e-hdrs-marks-face
  '((t :inherit font-lock-preprocessor-face))
  "Face for the mark in the headers list."
  :group 'mu4e-faces)

(defface mu4e-system-face
  '((t :inherit font-lock-comment-face :slant italic))
  "Face for system message (such as the footers for message
headers)."
  :group 'mu4e-faces)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; internal variables / constants

(defconst mu4e-header-names
  '( (:attachments   .  "Attach")
     (:bcc           .  "Bcc")
     (:cc            .  "Cc")
     (:date          .  "Date")
     (:flags         .  "Flgs")
     (:from          .  "From")
     (:from-or-to    .  "From/To")
     (:maildir       .  "Maildir")
     (:path          .  "Path")
     (:subject       .  "Subject")
     (:to            .  "To"))
"A alist of all possible header fields; this is used in the UI (the
column headers in the header list, and the fields the message
view). Most fields should be self-explanatory. A special one is
`:from-or-to', which is equal to `:from' unless `:from' matches ,
in which case it will be equal to `:to'.)")


;; mm startup function ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
(defun mu4e-create-maildir-maybe (dir)
  "Offer to create DIR if it does not exist yet. Return t if the
dir already existed, or has been created, nil otherwise."
  (if (and (file-exists-p dir) (not (file-directory-p dir)))
    (error "%s exists, but is not a directory." dir))
  (cond
    ((file-directory-p dir) t)
    ((yes-or-no-p (format "%s does not exist yes. Create now?" dir))
      (mu4e-proc-mkdir dir))
    (t nil)))

(defun mu4e-check-requirements ()
  "Check for the settings required for running mu4e."
  (unless mu4e-maildir
    (error "Please set `mu4e-maildir' to the full path to your
    Maildir directory."))
  (unless (mu4e-create-maildir-maybe mu4e-maildir)
    (error "%s is not a valid maildir directory" mu4e-maildir))
  (dolist (var '( mu4e-sent-folder
		  mu4e-drafts-folder
		  mu4e-trash-folder))
    (unless (and (boundp var) (symbol-value var))
      (error "Please set %S" var))
    (let* ((dir (symbol-value var)) (path (concat mu4e-maildir dir)))
      (unless (string= (substring dir 0 1) "/")
	(error "%S must start with a '/'" dir))
      (unless (mu4e-create-maildir-maybe path)
	(error "%s (%S) does not exist" path var)))))

(defun mu4e ()
  "Start mm. We do this by sending a 'ping' to the mu server
process, and start the main view if the 'pong' we receive from the
server has the expected values."
  (interactive)
  (if (buffer-live-p (get-buffer mu4e-main-buffer-name))
    (switch-to-buffer mu4e-main-buffer-name)
    (mu4e-check-requirements)
    ;; explicit version checks are a bit questionable,
    ;; better to check for specific features
    (if (< emacs-major-version 23)
	(error "Emacs >= 23.x is required for mu4e")
	(progn
	  (setq mu4e-proc-pong-func
	    (lambda (version doccount)
	      (unless (string= version mu4e-mu-version)
		(error "mu server has version %s, but we need %s"
		  version mu4e-mu-version))
	      (mu4e-main-view)
	      (message "Started mu4e with %d message%s in store"
		doccount (if (= doccount 1) "" "s"))))
	  (mu4e-proc-ping)))))

(defun mu4e-get-maildirs (parentdir)
  "List the maildirs under PARENTDIR." ;; TODO: recursive?
  (let* ((files (directory-files parentdir))
	  (maildirs ;;
	    (remove-if
	      (lambda (file)
		(let ((path (concat parentdir "/" file)))
		  (cond
		    ((string-match "^\\.\\{1,2\\}$" file)  t) ;; remove '..' and '.'
		    ((not (file-directory-p path)) t)   ;; remove non-dirs
		    ((not ;; remove non-maildirs
		       (and (file-directory-p (concat path "/cur"))
			 (file-directory-p (concat path "/new"))
			 (file-directory-p (concat path "/tmp")))) t)
		    (t nil) ;; otherwise, it's probably maildir
		    )))
	      files)))
    (map 'list (lambda(dir) (concat "/" dir)) maildirs)))

(defun mu4e-ask-maildir (prompt)
  "Ask the user for a shortcut (using PROMPT) as defined in
`mu4e-maildir-shortcuts', then return the corresponding folder
name. If the special shortcut 'o' (for _o_ther) is used, or if
`mu4e-maildir-shortcuts is not defined, let user choose from all
maildirs under `mu4e-maildir."
  (unless mu4e-maildir (error "`mu4e-maildir' is not defined"))
  (if (not mu4e-maildir-shortcuts)
    (ido-completing-read prompt (mu4e-get-maildirs mu4e-maildir))
    (let* ((mlist (append mu4e-maildir-shortcuts '(("ther" . ?o))))
	    (fnames
	      (mapconcat
		(lambda (item)
		  (concat
		    "["
		    (propertize (make-string 1 (cdr item))
		      'face 'mu4e-view-link-face)
		    "]"
		    (car item)))
		mlist ", "))
	    (kar (read-char (concat prompt fnames))))
      (if (= kar ?o) ;; user chose 'other'?
	(ido-completing-read prompt (mu4e-get-maildirs mu4e-maildir))
	(or
	  (car-safe (find-if
		      (lambda (item)
			(= kar (cdr item)))
		      mu4e-maildir-shortcuts))
	  (error "Invalid shortcut '%c'" kar))))))



(defun mu4e-ask-bookmark (prompt &optional kar)
  "Ask the user for a bookmark (using PROMPT) as defined in
`mu4e-bookmarks', then return the corresponding query."
  (unless mu4e-bookmarks (error "`mu4e-bookmarks' is not defined"))
  (let* ((bmarks
	   (mapconcat
	     (lambda (bm)
	       (let ((query (nth 0 bm)) (title (nth 1 bm)) (key (nth 2 bm)))
		 (concat
		   "[" (propertize (make-string 1 key)
			 'face 'mu4e-view-link-face)
		   "]"
		   title))) mu4e-bookmarks ", "))
	  (kar (read-char (concat prompt bmarks))))
    (mu4e-get-bookmark-query kar)))

(defun mu4e-get-bookmark-query (kar)
  "Get the corresponding bookmarked query for shortcut character
KAR, or raise an error if none is found."
 (let ((chosen-bm
	 (find-if
	   (lambda (bm)
	     (= kar (nth 2 bm)))
	   mu4e-bookmarks)))
   (if chosen-bm
     (nth 0 chosen-bm)
     (error "Invalid shortcut '%c'" kar))))

(defun mu4e-new-buffer (bufname)
  "Return a new buffer BUFNAME; if such already exists, kill the
old one first."
  (when (get-buffer bufname)
    (progn
      (message (format "Killing %s" bufname))
      (kill-buffer bufname)))
  (get-buffer-create bufname))



;;; converting flags->string and vice-versa ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(defun mu4e-flags-to-string (flags)
  "Remove duplicates and sort the output of `mu4e-flags-to-string-raw'."
  (concat
    (sort (remove-duplicates
	    (append (mu4e-flags-to-string-raw flags) nil)) '>)))

(defun mu4e-flags-to-string-raw (flags)
  "Convert a list of flags into a string as seen in Maildir
message files; flags are symbols draft, flagged, new, passed,
replied, seen, trashed and the string is the concatenation of the
uppercased first letters of these flags, as per [1]. Other flags
than the ones listed here are ignored.

Also see `mu4e-flags-to-string'.

\[1\]: http://cr.yp.to/proto/maildir.html"
  (when flags
    (let ((kar (case (car flags)
		 ('draft     ?D)
		 ('flagged   ?F)
		 ('new       ?N)
		 ('passed    ?P)
		 ('replied   ?R)
		 ('seen      ?S)
		 ('trashed   ?T)
		 ('attach    ?a)
		 ('encrypted ?x)
		 ('signed    ?s)
		 ('unread    ?u))))
      (concat (and kar (string kar))
	(mu4e-flags-to-string-raw (cdr flags))))))


(defun mu4e-string-to-flags (str)
  "Remove duplicates from the output of `mu4e-string-to-flags-1'"
  (remove-duplicates (mu4e-string-to-flags-1 str)))

(defun mu4e-string-to-flags-1 (str)
  "Convert a string with message flags as seen in Maildir
messages into a list of flags in; flags are symbols draft,
flagged, new, passed, replied, seen, trashed and the string is
the concatenation of the uppercased first letters of these flags,
as per [1]. Other letters than the ones listed here are ignored.
Also see `mu/flags-to-string'.
\[1\]: http://cr.yp.to/proto/maildir.html"
  (when (/= 0 (length str))
    (let ((flag
	    (case (string-to-char str)
	      (?D   'draft)
	      (?F   'flagged)
	      (?P   'passed)
	      (?R   'replied)
	      (?S   'seen)
	      (?T   'trashed))))
      (append (when flag (list flag))
	(mu4e-string-to-flags-1 (substring str 1))))))


(defun mu4e-display-size (size)
  "Get a string representation of SIZE (in bytes)."
  (cond
    ((>= size 1000000) (format "%2.1fM" (/ size 1000000.0)))
    ((and (>= size 1000) (< size 1000000))
      (format "%2.1fK" (/ size 1000.0)))
    ((< size 1000) (format "%d" size))
    (t (propertize "?" 'face 'mu4e-system-face))))


(defun mu4e-body-text (msg)
  "Get the body in text form for this message, which is either :body-txt,
or if not available, :body-html converted to text. By default, it
uses the emacs built-in `html2text'. Alternatively, if
`mu4e-html2text-command' is non-nil, it will use that. Normally,
function prefers the text part, but this can be changed by setting
`mu4e-view-prefer-html'."
  (let* ((txt (plist-get msg :body-txt))
	 (html (plist-get msg :body-html))
	  (body))
    ;; is there an appropriate text body?
    (when (and txt
	    (not (and mu4e-view-prefer-html html))
	    (> (* 10 (length txt))
	      (if html (length html) 0))) ;; real text part?
      (setq body txt))
    ;; no body yet? try html
    (unless body 
      (when html
	(setq body 
	  (with-temp-buffer
	    (insert html)
	    ;; if defined, use the external tool
	    (if mu4e-html2text-command 
	      (shell-command-on-region (point-min) (point-max)
		mu4e-html2text-command nil t)
	      ;; otherwise...
	      (html2text))
	    (buffer-string)))))
    ;; still no body?
    (unless body
      (setq body (propertize "No body" 'face 'mu4e-system-face)))
    ;; and finally, remove some crap from the remaining string.
    (replace-regexp-in-string "[ ]" " " body nil nil nil)))

(defconst mu4e-get-mail-name "*mu4e-get-mail*"
  "*internal* Name of the process to retrieve mail")

(defun mu4e-retrieve-mail-update-db-proc (buf)
  "Try to retrieve mail (using `mu4e-get-mail-command'), with
output going to BUF if not nil, or discarded if nil. After
retrieving mail, update the database."
  (unless mu4e-get-mail-command
    (error "`mu4e-get-mail-command' is not defined"))
  (message "Retrieving mail...")
  (let* ((proc (start-process-shell-command
		 mu4e-get-mail-name buf mu4e-get-mail-command)))
    (set-process-sentinel proc
      (lambda (proc msg)
	(let ((buf (process-buffer proc)))
	  (when  (buffer-live-p buf)
	    (mu4e-proc-index mu4e-maildir)
	    (kill-buffer buf)))))))

(defun mu4e-retrieve-mail-update-db ()
  "Try to retrieve mail (using the user-provided shell command),
and update the database afterwards"
  (interactive)
  (unless mu4e-get-mail-command
    (error "`mu4e-get-mail-command' is not defined"))
  (let ((buf (get-buffer-create mu4e-update-buffer-name))
	 (win
	   (split-window (selected-window)
	     (- (window-height (selected-window)) 8))))
    (with-selected-window win
      (switch-to-buffer buf)
      (set-window-dedicated-p win t)
      (erase-buffer)
      (mu4e-retrieve-mail-update-db-proc buf))))


(defun mu4e-display-manual ()
  "Display the mu4e manual page for the current mode, or go to the
top level if there is none."
  (interactive)
  (info (case major-mode
	  ('mu4e-main-mode    "(mu4e)Main view")
	  ('mu4e-hdrs-mode    "(mu4e)Headers view")
	  ('mu4e-view-mode    "(mu4e)Message view")
	  (t                 "mu4e"))))

(defun mu4e-quit()
  "Quit the mm session."
  (interactive)
  (when (y-or-n-p "Are you sure you want to quit? ")
    (message nil)
    (mu4e-kill-proc)
    (kill-buffer)))


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(provide 'mu4e)
