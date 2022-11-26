(defun geiser-mesche--binary ()
  "Return the path to Mesche."
  (message "GET BINARY")
  mesche-cli-bin-path)

(defun geiser-mesche--parameters ()
  "Return the parameters to invoke the Mesche REPL."
  (list "repl"))

(defconst geiser-mesche--prompt-regexp "mesche:.+> ")

(defun geiser-mesche--enter-command (module)
  "Formats a command to enter MODULE."
  (format "(module-enter %s)" module))

(defun geiser-mesche--geiser-procedure (proc &rest args)
  "Transform PROC in string for a scheme procedure using ARGS."
  (message "PROC: %s / ARGS: %S" proc args)
  (cl-case proc
    ((eval compile)
     (let ((form (mapconcat 'identity (cdr args) " "))
           (module (cond ((string-equal "'()" (car args))
                          "'()")
                         ((and (car args))
                          (concat "'" (car args)))
                         (t
                          "#f"))))
       (format "(geiser:eval %s '%s)" module form)))
    ((load-file compile-file)
     (format "(geiser:load-file %s)" (car args)))
    ((no-values)
     "(geiser:no-values)")
    (t
     (let ((form (mapconcat 'identity args " ")))
       (format "(geiser:%s %s)" proc form)))))


(define-geiser-implementation mesche
  (binary geiser-mesche--binary)
  (arglist geiser-mesche--parameters)
  ;; (version-command geiser-mit--version)
  ;; (minimum-version geiser-mit-minimum-version)
  ;; (repl-startup geiser-mit--startup)
  (prompt-regexp geiser-mesche--prompt-regexp)
  ;; (debugger-prompt-regexp geiser-mit--debugger-prompt-regexp)
  ;; (enter-debugger geiser-mit--enter-debugger)
  (marshall-procedure geiser-mesche--geiser-procedure)
  ;; (find-module geiser-mit--get-module)
  ;; (enter-command geiser-mit--enter-command)
  ;; (exit-command geiser-mit--exit-command)
  ;; (import-command geiser-mit--import-command)
  ;; (find-symbol-begin geiser-mit--symbol-begin)
  ;; (display-error geiser-mit--display-error)
  ;; (external-help geiser-mit--manual-look-up)
  ;; (check-buffer geiser-mit--guess)
  ;; (keywords geiser-mit--keywords)
  ;; (case-sensitive geiser-mit-case-sensitive-p)
  )

(geiser-implementation-extension 'mesche "msc")
(geiser-activate-implementation 'mesche)

(provide 'geiser-mesche)
