(ns com.billpiel.guildsman.tensor-scope
  (:require [com.billpiel.guildsman.tensor :as tsr]
            [com.billpiel.guildsman.data-type :as dt]
            [com.billpiel.guildsman.shape :as sh])
  (:import [com.billpiel.guildsman.tensor PValueProvider]
           [com.billpiel.guildsman TensorNI]))

;; TODO use ThreadLocal instead of dyn scope to avoid future-conveyor

(def global-scope (atom nil))
(def ^:dynamic *scope* {:type :global})

;; TODO move to tensor.clj??? -- I forget why -- volatile valid flags?
(defonce state (atom {:handles {}
                      :scopes {}
                      :delete []}))

#_ (def state (atom {:handles {}
                     :scopes {}
                     :delete []}))

(defn PValueProvider?
  [v]
  (and (instance? java.lang.Object v)
       (satisfies? tsr/PValueProvider v)))

(defn ->handle
  [v]
  (when (PValueProvider? v)
    (tsr/getHandle v)))

(defn find-native-handles
  [v]
  (->> v
       (tree-seq #(and (coll? %)
                       (not (PValueProvider? %)))
                 seq)
       (keep ->handle)))

(defn- remove-scope-from-hnds
  [state-handles scope-id hnds]
  (reduce (fn [agg hnd] (update agg hnd disj scope-id))
          state-handles
          hnds))

(defn- mark-deletable-hnds
  [{:keys [handles] :as state} hnds]
  (let [deletable (->> hnds
                       (select-keys handles)
                       (filter (comp empty? second))
                       (mapv first))]
    (-> state
        (update :handles (partial apply dissoc) deletable)
        (assoc :delete deletable))))

(defn- delete-tensor!
  [hnd]
  (TensorNI/delete hnd))

(defn- delete-marked-hnds!
  [{:keys [delete] :as state}]
  (doseq [t delete]
    (delete-tensor! t)))

(defn get-scope [& [scope]]
  (let [sc (or scope *scope*)
        ty (:type sc)]
    (case ty
        nil (throw (Exception. "No tensor scope has been set."))
        :global @global-scope
        sc)))

(defn- close-scope*
  [state id]
  (if-let [hnds (some-> state :scopes id not-empty)]
    (-> state
        (update :scopes dissoc id)
        (update :handles remove-scope-from-hnds id hnds)
        (mark-deletable-hnds hnds))
    (assoc state
           :delete [])))

(defn close-scope!
  [{ty :type id :id :as scope}]
  (when (= ty :standard)
    (-> state
        (swap! close-scope* id)
        delete-marked-hnds!)))

(defn close-global-scope! []
  (close-scope! (get-scope {:type :global})))

(defn set-global-scope!
  [scope]
  (close-global-scope!)
  (reset! global-scope (assoc scope
                              :parent nil)))

(defn- mk-id []
  (-> (gensym "tensor-scope")
      name
      keyword))

(defn mk-scope [ty]
  {:type ty
   :id (mk-id)
   :parent (dissoc *scope*
                   :parent)})

(defn mk-orphan-scope [ty]
  (binding [*scope* nil]
    (mk-scope ty)))

(defn set-global-standard-scope! []
  (set-global-scope! (mk-scope :standard)))

(defn set-global-conversion-scope! []
  (set-global-scope! (mk-scope :conversion)))

(defn walk-tensors
  [f v]
  (cond (PValueProvider? v) (f v)
        
        (sequential? v)
        (->> v
             (map (partial walk-tensors f))
             (into (empty v)))

        (map? v)
        (into {}
              (for [[k v'] v]
                [(walk-tensors f k) (walk-tensors f v')]))

        :else v))

(defn convert-if-tensor [v]
  (if (->handle v)
    (tsr/->clj v)
    v))

(defn walk-convert-tensors [v] (walk-tensors convert-if-tensor v))

(def conj-set (fnil conj #{}))

(defn- add-to-scope**
  [scope-id state handle]
  (-> state
      (update-in [:handles handle]
                 conj-set
                 scope-id)
      (update-in [:scopes scope-id]
                 conj-set
                 handle)))

(defn add-to-scope*
  [state scope-id tensors]
  (reduce (partial add-to-scope** scope-id)
          state
          tensors))

(defn add-to-scope!
  ([v]
   (add-to-scope! (get-scope) v))
  ([{ty :type id :id :as scope} v]
   (case ty
     :standard (do (some->> v
                            find-native-handles
                            not-empty
                            (swap! state add-to-scope* id))
                   v)
     nil (if (->> v
                  find-native-handles
                  not-empty)
           (throw (Exception. "No tensor scope. Cannot create native tensor value without a tensor scope." ))
           v)
     :conversion (walk-convert-tensors v))))

(defmacro with-this-scope
  [scope & body]
  `(let [scope# ~scope]
     (binding [*scope* scope#]
       (try
         (let [parent# (some-> scope#
                               :parent
                               get-scope)]
           (->> (do ~@body)
                (add-to-scope! parent#)))
         (finally
           (close-scope! scope#))))))

(defmacro with-scope
  [& body]
  `(with-this-scope (mk-scope :standard)
     ~@body))

(defmacro with-conversion-scope
  [& body]
  `(with-this-scope (mk-scope :conversion)
     ~@body))

(defmacro with-scope-containing
  [tensors & body]
  `(with-this-scope (mk-scope :standard)
     (add-to-scope! (get-scope) ~tensors)
     ~@body))

(defn get-tensor-by-value ^PValueProvider
  [v]
  (with-scope
    (-> v
        tsr/create-from-value
        add-to-scope!)))

(defn get-tensor-by-handle ^PValueProvider
  [hnd]
  (with-scope
    (-> hnd
        tsr/create-from-handle
        add-to-scope!)))
