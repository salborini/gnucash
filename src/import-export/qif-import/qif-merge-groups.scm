;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;  qif-merge-groups.scm
;;;  eliminate duplicate xtns in a new (imported) account group 
;;;
;;;  Copyright 2001 Bill Gribble <grib@billgribble.com> 
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (gnc:account-tree-get-transactions root)
  (let ((query (gnc:malloc-query))
        (xtns #f))

    (gnc:query-set-book query (gnc:account-get-book root))

    ;; we want to find all transactions with every split inside the
    ;; account group.
    (gnc:query-add-account-match query
                                 (gnc:account-get-descendants root)
                                 'guid-match-any 'query-and)

    (set! xtns (gnc:query-get-transactions query 'query-txn-match-all))
    
    ;; lose the query 
    (gnc:free-query query)
    xtns))


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;  gnc:account-tree-find-duplicates 
;;  detect redundant splits/xtns from 'new' and return 
;;  them in a list. 
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(define (gnc:account-tree-find-duplicates old-root new-root window)
  ;; get all the transactions in the new group, then iterate over them
  ;; trying to find matches in the new group.  If there are matches, 
  ;; push the matches onto a list. 
  (let* ((new-xtns (gnc:account-tree-get-transactions new-root))
	 (progress-dialog #f)
	 (work-to-do (length new-xtns))
	 (work-done 0)
         (matches '()))    
    
    (if (> work-to-do 100)
	(begin 
	  (set! progress-dialog (gnc:progress-dialog-new window #f))
	  (gnc:progress-dialog-set-title progress-dialog (_ "Progress"))
	  (gnc:progress-dialog-set-heading progress-dialog
					   (_ "Finding duplicate transactions..."))))

    ;; for each transaction in the new account tree, build a query that could
    ;; match possibly similar transactions.
    (for-each
     (lambda (xtn) 
       (let ((query (gnc:malloc-query)))         
	 (set! work-done (+ 1 work-done))
	 (if progress-dialog 
	     (begin 
	       (gnc:progress-dialog-set-value 
		progress-dialog (/ work-done work-to-do))
	       (gnc:progress-dialog-update progress-dialog))) 

	 (gnc:query-set-book query (gnc:account-get-book old-root))

	 ;; first, we want to find only transactions from the old group.
	 (gnc:query-add-account-match query
				      (gnc:account-get-descendants old-root)
				      'guid-match-any 'query-and)
         
         ;; the date should be close to the same.. +/- a week. 
         (let ((date (gnc:transaction-get-date-posted xtn)))               
           (gnc:query-add-date-match-timepair
            query #t (decdate date WeekDelta) #t (incdate date WeekDelta)
            'query-and))
         
         ;; for each split in the transaction, add a term to match the 
         ;; properties of one split 
         (let ((q-splits (gnc:malloc-query)))
           (for-each 
            (lambda (split)
              (let ((sq (gnc:malloc-query)))
		(gnc:query-set-book sq (gnc:account-get-book old-root))
                
                ;; we want to match the account in the old account
                ;; tree that has the same name as an account in the
                ;; new account tree.  If there's not one (new
                ;; account), the match will be NULL and we know the
                ;; query won't find anything.  optimize this later.
                (gnc:query-add-single-account-match 
                 sq 
                 (gnc:get-account-from-full-name
                  old-root (gnc:account-get-full-name 
                             (gnc:split-get-account split)))
                 'query-and)
                
                ;; we want the value for the split to match the value
                ;; the old-root split.  We should really check for
                ;; fuzziness.
                (gnc:query-add-value-match 
                 sq (gnc:split-get-value split)
                 'amt-sgn-match-either 'query-compare-equal
                 'query-and)
                
                ;; now merge into the split query.  Reminder: q-splits
                ;; is set up to match any split that matches any split
                ;; in the current xtn; every split in an old transaction
                ;; must pass that filter.
                (let ((q-new (gnc:query-merge q-splits sq 'query-or)))
                  (gnc:free-query q-splits)
                  (gnc:free-query sq)
                  (set! q-splits q-new))))
            (gnc:transaction-get-splits xtn))
           
           ;; now q-splits will match any split that is the same as one
           ;; split in the old-root xtn.  Merge it in.
           (let ((q-new (gnc:query-merge query q-splits 'query-and)))
             (gnc:free-query query)
             (gnc:free-query q-splits)
             (set! query q-new)))
         
         ;; now that we have built a query, get transactions in the old
         ;; account tree that matches it.
         (let ((old-xtns (gnc:query-get-transactions query 'query-txn-match-all)))
           (set! old-xtns (map 
                           (lambda (elt)
                             (cons elt #f)) old-xtns))
           
           ;; if anything matched the query, push it onto the matches list 
           ;; along with the transaction
           (if (not (null? old-xtns))
               (set! matches (cons (cons xtn old-xtns) matches))))
         (gnc:free-query query)))
     new-xtns)
    
    ;; get rid of the progress dialog 
    (if progress-dialog
	(gnc:progress-dialog-destroy progress-dialog))

    ;; return the matches 
    matches))
  
(define (gnc:prune-matching-transactions match-list)
  (for-each 
   (lambda (match)
     (let ((new-xtn (car match))
           (matches (cdr match))
           (do-delete #f))
       (for-each 
        (lambda (old)
          (if (cdr old)
              (set! do-delete #t)))
        matches)
       (if do-delete 
           (begin 
             (gnc:transaction-begin-edit new-xtn)
             (gnc:transaction-destroy new-xtn)
             (gnc:transaction-commit-edit new-xtn)))))
   match-list))

(define (gnc:account-tree-catenate-and-merge old-root new-root)
  ;; stuff the new accounts into the old account tree and merge the accounts
  (gnc:account-join-children old-root new-root)
  (gnc:account-begin-edit new-root)
  (gnc:account-destroy new-root)
  (gnc:account-merge-children old-root))
